#!/usr/bin/env python3

# Copyright 2025 SYSFly Co.
# SPDX-License-Identifier: Apache-2.0

import sys
import yaml
import subprocess
import re

def run_command(cmd, cwd=None):
    """Run shell command and return output"""
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        return result.returncode, result.stdout.strip(), result.stderr.strip()
    except Exception as e:
        return 1, "", str(e)

def get_changed_files(base_sha, head_sha):
    """Get list of changed files between commits"""
    cmd = f"git diff --name-only {base_sha}...{head_sha}"
    ret, stdout, stderr = run_command(cmd)
    if ret != 0:
        print(f"Error getting changed files: {stderr}")
        return []
    return stdout.split('\n') if stdout else []

def parse_manifest(file_path):
    """Parse west manifest file"""
    try:
        with open(file_path, 'r') as f:
            return yaml.safe_load(f)
    except Exception as e:
        print(f"Error parsing manifest {file_path}: {e}")
        return None

def get_manifest_at_commit(manifest_path, commit_sha):
    """Get manifest content at specific commit"""
    cmd = f"git show {commit_sha}:{manifest_path}"
    ret, stdout, stderr = run_command(cmd)
    if ret != 0:
        print(f"Error getting manifest at {commit_sha}: {stderr}")
        return None
    try:
        return yaml.safe_load(stdout)
    except Exception as e:
        print(f"Error parsing manifest from commit {commit_sha}: {e}")
        return None

def check_pr_revisions(manifest_data):
    """Check if any revisions point to pull requests"""
    dnm_found = False
    pr_pattern = re.compile(r'pull/\d+/head|refs/pull/\d+')

    if not manifest_data or 'manifest' not in manifest_data:
        return dnm_found
    projects = manifest_data['manifest'].get('projects', [])
    for project in projects:
        revision = project.get('revision', '')
        if pr_pattern.search(str(revision)):
            print(f"WARNING: Project '{project.get('name', 'unknown')}' uses PR revision: {revision}")
            dnm_found = True
    return dnm_found

def compare_manifests(old_manifest, new_manifest):
    """Compare two manifest files and report differences"""
    changes = []
    if not old_manifest or not new_manifest:
        return changes
    old_projects = {p['name']: p for p in old_manifest.get('manifest', {}).get('projects', [])}
    new_projects = {p['name']: p for p in new_manifest.get('manifest', {}).get('projects', [])}
    # Check for changed projects
    for name, new_proj in new_projects.items():
        if name in old_projects:
            old_proj = old_projects[name]
            old_rev = old_proj.get('revision', 'main')
            new_rev = new_proj.get('revision', 'main')
            if old_rev != new_rev:
                changes.append({
                    'project': name,
                    'old_revision': old_rev,
                    'new_revision': new_rev,
                    'type': 'changed'
                })
        else:
            changes.append({
                'project': name,
                'old_revision': None,
                'new_revision': new_proj.get('revision', 'main'),
                'type': 'added'
            })
    # Check for removed projects
    for name in old_projects:
        if name not in new_projects:
            changes.append({
                'project': name,
                'old_revision': old_projects[name].get('revision', 'main'),
                'new_revision': None,
                'type': 'removed'
            })
    return changes

def main():
    if len(sys.argv) != 3:
        print("Usage: manifest_check.py <base_sha> <head_sha>")
        sys.exit(1)
    base_sha = sys.argv[1]
    head_sha = sys.argv[2]
    manifest_path = 'west.yml'
    print(f"Checking manifest changes between {base_sha} and {head_sha}")
    # Check if manifest file was changed
    changed_files = get_changed_files(base_sha, head_sha)
    manifest_changed = any(manifest_path in f for f in changed_files)
    if not manifest_changed:
        print("No manifest changes detected.")
        return 0
    print("Manifest file changed, performing checks...")
    # Get old and new manifest data
    old_manifest = get_manifest_at_commit(manifest_path, base_sha)
    new_manifest = parse_manifest(manifest_path)
    if not new_manifest:
        print("ERROR: Could not parse current manifest file")
        return 1
    # Check for PR revisions (DNM check)
    dnm_found = check_pr_revisions(new_manifest)
    if dnm_found:
        print("ERROR: Manifest contains PR revisions - this should not be merged!")
        return 1
    # Compare manifests
    changes = compare_manifests(old_manifest, new_manifest)
    if changes:
        print("\nManifest Changes:")
        print("=" * 50)
        for change in changes:
            if change['type'] == 'changed':
                print(f"• {change['project']}: {change['old_revision']} → {change['new_revision']}")
            elif change['type'] == 'added':
                print(f"• {change['project']}: ADDED (revision: {change['new_revision']})")
            elif change['type'] == 'removed':
                print(f"• {change['project']}: REMOVED (was: {change['old_revision']})")
    else:
        print("No project revision changes detected.")
    print("Manifest check completed successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
