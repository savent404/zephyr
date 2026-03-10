# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

'''west export command: export a stripped, portable Zephyr build project.

Produces a self-contained project directory that can be compiled on a
different machine using only Ninja and the matching Zephyr SDK.
'''

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand

from build_helpers import find_build_dir, is_zephyr_build, \
    FIND_BUILD_DIR_DESCRIPTION
from zcmake import CMakeCache

EXPORT_DESCRIPTION = '''\
Export a stripped Zephyr build as a portable Ninja project.

The exported directory contains only the source files, headers and
generated files that are actually used by the build.  A Makefile,
toolchain.env and setup.py are generated so the project can be rebuilt
on another machine with the matching Zephyr SDK.

''' + FIND_BUILD_DIR_DESCRIPTION

# Files/dirs inside the build directory that are generated artifacts
# we want to copy verbatim into the export.
_GENERATED_COPY_GLOBS = [
    'zephyr/include/generated',
    'zephyr/isr_tables.c',
    'zephyr/isr_tables_swi.ld',
    'zephyr/isr_tables_vt.ld',
    'zephyr/linker.cmd',
    'zephyr/linker_zephyr_pre0.cmd',
    'zephyr/misc/generated',
    'zephyr/.config',
]


class StripedExport(WestCommand):

    def __init__(self):
        super().__init__(
            'export',
            # Keep in sync with west-commands.yml.
            'export a stripped build project for another machine',
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False)

    # ── argparse ────────────────────────────────────────────────────────

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description)
        parser.add_argument('-b', '--board',
                            help='board to build for (used when auto-building)')
        parser.add_argument('source_dir', nargs='?', default=None,
                            help='application source directory')
        parser.add_argument('-o', '--output-dir', required=True,
                            help='directory to export the stripped project to')
        parser.add_argument('-d', '--build-dir', default=None,
                            help='existing build directory to export from')
        parser.add_argument(
            '--strip-cfg', '--strip-config', '--strip-inactive-branches',
            '--strip-inactivate-branches',
            dest='strip_inactive_branches', action='store_true',
            help='strip inactive #if CONFIG_* branches from exported source '
                 'files using zephyr/include/generated/zephyr/autoconf.h '
                 '(short: --strip-cfg)')
        return parser

    # ── main entry ──────────────────────────────────────────────────────

    def do_run(self, args, unknown_args):
        self.args = args
        build_dir = self._ensure_build()
        output_dir = Path(args.output_dir).resolve()

        if output_dir.exists():
            shutil.rmtree(output_dir)
        output_dir.mkdir(parents=True)

        self.inf(f'Exporting build from {build_dir} → {output_dir}')

        cmake_vars = self._parse_cmake_cache(build_dir)
        path_mapping = self._build_path_mapping(cmake_vars, build_dir)
        used_files = self._collect_used_source_files(build_dir, cmake_vars)

        n_src = self._copy_source_files(used_files, path_mapping, output_dir)
        n_gen = self._copy_generated_files(build_dir, output_dir)
        self._rewrite_ninja_files(build_dir, path_mapping, output_dir,
                                  cmake_vars)
        n_pruned = self._prune_unused_export_files(
            used_files, path_mapping, output_dir)
        strip_stats = {
            'files': 0,
            'lines_removed': 0,
            'lines_before': 0,
            'lines_after': 0,
        }
        if self.args.strip_inactive_branches:
            strip_stats = self._strip_inactive_branches(output_dir)
        self._generate_toolchain_env(cmake_vars, output_dir)
        self._generate_setup_py(output_dir)
        self._generate_makefile(output_dir)

        self.inf(f'Export complete: {n_src} source files, '
                 f'{n_gen} generated files.')
        self.inf(f'Pruned {n_pruned} unused header/linker files from export.')
        if self.args.strip_inactive_branches:
            self.inf('Stripped inactive CONFIG branches in '
                     f"{strip_stats['files']} files.")
            self.inf('Branch-strip line stats: '
                     f"removed {strip_stats['lines_removed']} lines, "
                     f"{strip_stats['lines_before']} -> "
                     f"{strip_stats['lines_after']} lines.")
            for ext in ('.c', '.h', '.s', '.ld', '.lds'):
                ext_stat = strip_stats['per_ext'].get(ext)
                if not ext_stat or ext_stat['files'] == 0:
                    continue
                ext_name = '.S' if ext == '.s' else ext
                self.inf('  '
                         f"{ext_name}: {ext_stat['files']} files, "
                         f"-{ext_stat['lines_removed']} lines "
                         f"({ext_stat['lines_before']} -> "
                         f"{ext_stat['lines_after']})")
        self.inf(f'To build:\n  cd {output_dir}\n  source toolchain.env\n'
                 f'  make')

    # ── phase 1: ensure a build exists ──────────────────────────────────

    @staticmethod
    def _resolve_source_dir(source_dir):
        if not source_dir:
            return None
        return str(Path(source_dir).resolve())

    def _validate_source_dir(self, source_dir):
        if not os.path.isdir(source_dir):
            self.die(f'source directory {source_dir} does not exist')
        if 'CMakeLists.txt' not in os.listdir(source_dir):
            self.die(f'{source_dir} does not contain a CMakeLists.txt')

    @staticmethod
    def _get_application_source_dir(cache_vars):
        for key in ('APP_DIR', 'APPLICATION_SOURCE_DIR',
                    'CMAKE_HOME_DIRECTORY'):
            value = cache_vars.get(key)
            if value:
                return str(Path(str(value)).resolve())
        return None

    @classmethod
    def _get_cached_build_context(cls, build_dir):
        cache = CMakeCache.from_build_dir(build_dir)
        source_dir = cls._get_application_source_dir(cache)
        board = cache.get('CACHED_BOARD')
        if board is not None:
            board = str(board)
        return source_dir, board

    @staticmethod
    def _build_matches_request(requested_source_dir, requested_board,
                               cached_source_dir, cached_board):
        if requested_source_dir:
            if not cached_source_dir:
                return False
            if Path(requested_source_dir).resolve() != Path(
                    cached_source_dir).resolve():
                return False

        if requested_board:
            if not cached_board or requested_board != cached_board:
                return False

        return True

    def _find_candidate_build_dir(self, requested_source_dir):
        if self.args.build_dir:
            build_dir = self.args.build_dir
        else:
            app = (os.path.basename(requested_source_dir)
                   if requested_source_dir else None)
            build_dir = find_build_dir(None,
                                       board=self.args.board,
                                       source_dir=requested_source_dir,
                                       app=app)
            if not build_dir:
                self.die('Unable to determine a default build folder. Check '
                         'your build.dir-fmt configuration option')

        return Path(build_dir).resolve()

    @staticmethod
    def _run_west_build(build_dir, source_dir, board, pristine=False):
        cmd = [sys.executable, '-m', 'west', 'build']
        if pristine:
            cmd.extend(['--pristine', 'always'])
        cmd.extend(['-b', board,
                    str(Path(source_dir).resolve()),
                    '-d', str(Path(build_dir).resolve())])
        subprocess.check_call(cmd)

    def _ensure_build(self):
        requested_source_dir = self._resolve_source_dir(self.args.source_dir)
        build_dir = self._find_candidate_build_dir(requested_source_dir)

        if build_dir.exists() and not build_dir.is_dir():
            self.die(f'build directory {build_dir} exists and is not a '
                     'directory')

        if is_zephyr_build(str(build_dir)):
            cached_source_dir, cached_board = self._get_cached_build_context(
                build_dir)
            if self._build_matches_request(requested_source_dir,
                                           self.args.board,
                                           cached_source_dir,
                                           cached_board):
                self.inf(f'Using existing build directory: {build_dir}')
                return str(build_dir)

            rebuild_source_dir = requested_source_dir or cached_source_dir
            rebuild_board = self.args.board or cached_board

            if not rebuild_source_dir:
                self.die('Existing build directory has no cached application '
                         'source; please specify source_dir')
            if not rebuild_board:
                self.die('Existing build directory has no cached board; '
                         '-b BOARD is required to refresh it')

            self._validate_source_dir(rebuild_source_dir)
            self.inf('Existing build directory does not match the requested '
                     'source/board; rebuilding it pristine before export: '
                     f'{build_dir}')
            self._run_west_build(build_dir, rebuild_source_dir,
                                 rebuild_board, pristine=True)
            self.inf(f'Using existing build directory: {build_dir}')
            return str(build_dir)

        # Auto-build
        if not requested_source_dir:
            self.die('No existing build directory found and no source_dir '
                     'given; cannot auto-build.')
        if not self.args.board:
            self.die('No existing build directory found; -b BOARD is required '
                     'for auto-build.')

        self._validate_source_dir(requested_source_dir)
        self.inf(f'No build directory; running west build -b {self.args.board} '
                 f'{requested_source_dir} -d {build_dir}')

        self._run_west_build(build_dir, requested_source_dir,
                             self.args.board)
        return str(build_dir)

    # ── phase 2: parse CMakeCache ───────────────────────────────────────

    @staticmethod
    def _parse_cmake_cache(build_dir):
        cache = CMakeCache.from_build_dir(build_dir)
        keys_of_interest = [
            'ZEPHYR_BASE',
            'APP_DIR',
            'APPLICATION_SOURCE_DIR',
            'APPLICATION_BINARY_DIR',
            'CMAKE_HOME_DIRECTORY',
            'ZEPHYR_SDK_INSTALL_DIR',
            'CMAKE_C_COMPILER',
            'CMAKE_AR',
            'CMAKE_RANLIB',
            'CMAKE_OBJCOPY',
            'CMAKE_OBJDUMP',
            'CMAKE_READELF',
            'CMAKE_SIZE',
            'CACHED_BOARD',
            'WEST_TOPDIR',
        ]
        result = {}
        for k in keys_of_interest:
            v = cache.get(k)
            if v is not None:
                result[k] = str(v)

        # Collect module source dirs (they have keys like *_SOURCE_DIR
        # pointing outside the build tree)
        build_dir_p = Path(build_dir).resolve()
        for entry in cache:
            name = entry.name
            if name.endswith('_SOURCE_DIR'):
                val = str(entry.value)
                vp = Path(val).resolve()
                if vp.exists() and not str(vp).startswith(str(build_dir_p)):
                    result[name] = val

        # Derive WEST_TOPDIR if not cached
        if 'WEST_TOPDIR' not in result and 'ZEPHYR_BASE' in result:
            result['WEST_TOPDIR'] = str(
                Path(result['ZEPHYR_BASE']).parent)

        return result

    # ── phase 3: path mapping ───────────────────────────────────────────

    @staticmethod
    def _build_path_mapping(cmake_vars, build_dir):
        '''Build a list of (abs_prefix, replacement) sorted longest first.'''
        mapping = []
        build_dir_s = str(Path(build_dir).resolve())

        # Build dir → empty (export root IS the build dir)
        mapping.append((build_dir_s, '.'))

        # Application source
        app_src = StripedExport._get_application_source_dir(cmake_vars)
        if app_src:
            mapping.append((app_src, 'src/app'))

        # Zephyr base
        zephyr_base = cmake_vars.get('ZEPHYR_BASE')
        if zephyr_base:
            mapping.append((str(Path(zephyr_base).resolve()), 'src/zephyr'))

        # Module source dirs
        for key, val in cmake_vars.items():
            if key.endswith('_SOURCE_DIR') and key not in (
                    'APPLICATION_SOURCE_DIR', 'APPLICATION_BINARY_DIR',
                    'ZEPHYR_BASE'):
                vp = Path(val).resolve()
                # Skip if under ZEPHYR_BASE or build_dir
                if zephyr_base and str(vp).startswith(
                        str(Path(zephyr_base).resolve())):
                    continue
                if str(vp).startswith(build_dir_s):
                    continue
                # Derive a short name for the module
                name = key.replace('_SOURCE_DIR', '').lower()
                mapping.append((str(vp), f'src/modules/{name}'))

        # WEST_TOPDIR (catch-all for anything under the workspace)
        west_topdir = cmake_vars.get('WEST_TOPDIR')
        if west_topdir:
            mapping.append((str(Path(west_topdir).resolve()),
                            'src/workspace'))

        # SDK → placeholder (resolved at setup.py time)
        sdk = cmake_vars.get('ZEPHYR_SDK_INSTALL_DIR')
        if sdk:
            mapping.append((str(Path(sdk).resolve()), '@@SDK_DIR@@'))

        # Sort longest prefix first to avoid partial substitution
        mapping.sort(key=lambda x: len(x[0]), reverse=True)
        return mapping

    # ── phase 4: collect used source files ──────────────────────────────

    @staticmethod
    def _collect_used_source_files(build_dir, cmake_vars):
        '''Return a set of absolute paths of every file referenced by the
        build (sources from compile_commands.json + headers from .d files).

        Ninja stores GCC dep-tracking results in its binary .ninja_deps
        database (``deps = gcc`` in the build rule).  ``ninja -t deps`` reads
        that database and reports only the headers/sources that GCC actually
        scanned during compilation — exclusively for .obj targets, never for
        CUSTOM_COMMAND targets.  This is the authoritative source for what
        headers are genuinely #included.

        Linker script fragments (.ld/.lds) are excluded: the *generated*
        linker.cmd (built from those fragments) is already copied verbatim via
        _GENERATED_COPY_GLOBS and is all that is needed for a rebuild.
        '''
        build_dir_p = Path(build_dir)
        used = set()

        # 1) Compilation units from compile_commands.json
        cc_json = build_dir_p / 'compile_commands.json'
        if cc_json.exists():
            with open(cc_json) as f:
                entries = json.load(f)
            for entry in entries:
                entry_dir = entry.get('directory') or str(build_dir_p)

                src = entry.get('file')
                if src:
                    if not os.path.isabs(src):
                        src = os.path.join(entry_dir, src)
                    used.add(os.path.normpath(src))

                # Also extract forced includes from compile flags
                # (-imacros/-include). compile_commands.json may represent
                # command lines either as a shell string or argv array.
                argv = entry.get('arguments')
                if not argv:
                    cmd = entry.get('command', '')
                    try:
                        argv = shlex.split(cmd)
                    except ValueError:
                        argv = []

                i = 0
                while i < len(argv):
                    arg = argv[i]
                    path_arg = None

                    if arg in ('-imacros', '-include'):
                        if i + 1 < len(argv):
                            path_arg = argv[i + 1]
                            i += 1
                    elif arg.startswith('-imacros='):
                        path_arg = arg.split('=', 1)[1]
                    elif arg.startswith('-include='):
                        path_arg = arg.split('=', 1)[1]
                    elif arg.startswith('-imacros') and arg != '-imacros':
                        path_arg = arg[len('-imacros'):]
                    elif arg.startswith('-include') and arg != '-include':
                        path_arg = arg[len('-include'):]

                    if path_arg:
                        if not os.path.isabs(path_arg):
                            path_arg = os.path.join(entry_dir, path_arg)
                        used.add(os.path.normpath(path_arg))

                    i += 1

        # 2) Headers actually #included during compilation, read from ninja's
        #    binary deps database.  ``ninja -t deps`` only reports .obj entries
        #    (i.e. real compiler invocations) — CUSTOM_COMMAND targets do not
        #    appear here because they don't set ``deps = gcc``.
        try:
            deps_out = subprocess.check_output(
                ['ninja', '-t', 'deps'],
                cwd=build_dir, stderr=subprocess.DEVNULL
            ).decode('utf-8', errors='replace')
            for raw_line in deps_out.splitlines():
                if not raw_line or not raw_line[0].isspace():
                    continue

                dep = raw_line.strip()
                if not dep:
                    continue
                if os.path.isabs(dep):
                    used.add(os.path.normpath(dep))
                else:
                    used.add(os.path.normpath(
                        str((build_dir_p / dep).resolve())))
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

        # Linker script source fragments — NOT needed for rebuild because the
        # generated linker.cmd is already included via _GENERATED_COPY_GLOBS.
        _LINKER_SCRIPT_EXTS = {'.ld', '.lds'}

        # Filter: keep only files that actually exist and are not linker
        # script source fragments. Files under build_dir are kept as well,
        # because many generated headers (e.g. include/generated/*) are
        # mandatory for compilation.
        result = set()
        for p in used:
            if not os.path.isfile(p):
                continue
            if os.path.splitext(p)[1].lower() in _LINKER_SCRIPT_EXTS:
                continue
            result.add(p)

        return result

    # ── phase 5: copy source files ──────────────────────────────────────

    @staticmethod
    def _copy_source_files(file_set, path_mapping, output_dir):
        count = 0
        for src_abs in sorted(file_set):
            dst_rel = StripedExport._map_source_to_export_rel(
                src_abs, path_mapping)
            if dst_rel is None:
                # Unmapped file – use 'src/external/<basename>'
                dst_rel = os.path.join('src/external',
                                       os.path.basename(src_abs))

            # Skip SDK files — they stay on the target SDK installation
            if dst_rel.startswith('@@SDK_DIR@@'):
                continue

            dst = output_dir / dst_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_abs, dst)
            count += 1
        return count

    @staticmethod
    def _map_source_to_export_rel(src_abs, path_mapping):
        for prefix, replacement in path_mapping:
            if src_abs.startswith(prefix + '/') or src_abs == prefix:
                rest = src_abs[len(prefix):]
                if rest.startswith('/'):
                    rest = rest[1:]
                return os.path.join(replacement, rest)
        return None

    # ── phase 6: copy generated build artifacts ─────────────────────────

    @staticmethod
    def _copy_generated_files(build_dir, output_dir):
        build_dir_p = Path(build_dir)
        count = 0
        for glob_pat in _GENERATED_COPY_GLOBS:
            src = build_dir_p / glob_pat
            if not src.exists():
                continue
            dst = output_dir / glob_pat
            if src.is_dir():
                shutil.copytree(src, dst, dirs_exist_ok=True)
                count += sum(1 for _ in src.rglob('*') if _.is_file())
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
                count += 1
        return count

    @staticmethod
    def _prune_unused_export_files(used_files, path_mapping, output_dir):
        '''Remove exported .h/.ld/.lds files that are not in the used set.'''
        prune_exts = {'.h', '.ld', '.lds'}
        tracked = set()
        for src_abs in used_files:
            dst_rel = StripedExport._map_source_to_export_rel(
                src_abs, path_mapping)
            if not dst_rel or dst_rel.startswith('@@SDK_DIR@@'):
                continue
            tracked.add(os.path.normpath(dst_rel))

        # Preserve generated files we explicitly copied. These are required
        # build artifacts even when they don't appear in compiler deps.
        for glob_pat in _GENERATED_COPY_GLOBS:
            generated = output_dir / glob_pat
            if not generated.exists():
                continue

            if generated.is_file():
                if generated.suffix.lower() in prune_exts:
                    tracked.add(os.path.normpath(
                        str(generated.relative_to(output_dir))))
                continue

            for path in generated.rglob('*'):
                if not path.is_file():
                    continue
                if path.suffix.lower() in prune_exts:
                    tracked.add(os.path.normpath(
                        str(path.relative_to(output_dir))))

        # Keep files explicitly referenced by ninja templates even if they are
        # not seen in compiler deps (e.g. generated helper headers consumed by
        # phony/custom targets).
        token_re = re.compile(r'[^\s:|]+')
        for ninja_rel in ('build.ninja.in', 'CMakeFiles/rules.ninja.in'):
            ninja_path = output_dir / ninja_rel
            if not ninja_path.exists():
                continue
            text = ninja_path.read_text(errors='replace')
            for token in token_re.findall(text):
                tok = token.strip()
                if tok.startswith('@@'):
                    continue
                tok = re.sub(r'^\$\{[^}]+\}', '', tok)
                if tok.startswith('./'):
                    tok = tok[2:]
                if not tok:
                    continue
                ext = os.path.splitext(tok)[1].lower()
                if ext in {'.h', '.ld', '.lds'}:
                    tracked.add(os.path.normpath(tok))

        pruned = 0
        for path in output_dir.rglob('*'):
            if not path.is_file():
                continue
            if path.suffix.lower() not in prune_exts:
                continue
            rel = os.path.normpath(str(path.relative_to(output_dir)))
            if rel in tracked:
                continue
            path.unlink()
            pruned += 1
        return pruned

    @staticmethod
    def _strip_inactive_branches(output_dir):
        '''Strip inactive #if CONFIG_* branches in exported source files.

        This is intentionally conservative: only expressions resolvable from
        autoconf.h are folded; anything complex/unknown is left untouched.
        '''
        autoconf = output_dir / 'zephyr/include/generated/zephyr/autoconf.h'
        if not autoconf.exists():
            return {
                'files': 0,
                'lines_removed': 0,
                'lines_before': 0,
                'lines_after': 0,
                'per_ext': {},
            }

        text = autoconf.read_text(errors='replace')
        enabled = set(re.findall(r'^#define\s+(CONFIG_[A-Za-z0-9_]+)\b',
                                 text, re.MULTILINE))

        candidates = []
        for ext in ('*.c', '*.h', '*.S', '*.ld', '*.lds'):
            candidates.extend((output_dir / 'src').rglob(ext))

        modified = 0
        lines_removed = 0
        lines_before = 0
        lines_after = 0
        per_ext = {}
        for p in candidates:
            if not p.is_file():
                continue
            src = p.read_text(errors='replace')
            new_src, changed, removed_cnt = StripedExport._strip_inactive_in_text(
                src, enabled)
            if changed:
                p.write_text(new_src)
                modified += 1
                lines_removed += removed_cnt
                lines_before += src.count('\n')
                lines_after += new_src.count('\n')
                ext = p.suffix.lower()
                ext_stat = per_ext.setdefault(ext, {
                    'files': 0,
                    'lines_removed': 0,
                    'lines_before': 0,
                    'lines_after': 0,
                })
                ext_stat['files'] += 1
                ext_stat['lines_removed'] += removed_cnt
                ext_stat['lines_before'] += src.count('\n')
                ext_stat['lines_after'] += new_src.count('\n')

        return {
            'files': modified,
            'lines_removed': lines_removed,
            'lines_before': lines_before,
            'lines_after': lines_after,
            'per_ext': per_ext,
        }

    @staticmethod
    def _strip_inactive_in_text(text, enabled_configs):
        lines = text.splitlines(keepends=True)
        out = []
        changed = False

        # Each frame: {parent, active, known, taken, preserve}
        stack = []

        def cur_active():
            return all(f['active'] for f in stack)

        for line in lines:
            m = re.match(r'^(\s*)#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$',
                         line)
            if not m:
                if cur_active():
                    out.append(line)
                else:
                    changed = True
                continue

            directive = m.group(2)
            tail = m.group(3).strip()

            if directive in ('if', 'ifdef', 'ifndef'):
                parent = cur_active()
                if directive == 'ifdef':
                    cond = StripedExport._eval_ifdef(tail, enabled_configs)
                elif directive == 'ifndef':
                    cond = StripedExport._eval_ifndef(tail, enabled_configs)
                else:
                    cond = StripedExport._eval_if_expr(tail, enabled_configs)

                if cond is None:
                    frame = {
                        'parent': parent,
                        'active': parent,
                        'known': False,
                        'taken': False,
                        'preserve': True,
                    }
                    stack.append(frame)
                    if parent:
                        out.append(line)
                    else:
                        changed = True
                else:
                    frame = {
                        'parent': parent,
                        'active': parent and cond,
                        'known': True,
                        'taken': bool(cond),
                        'preserve': False,
                    }
                    stack.append(frame)
                    changed = True
                continue

            if not stack:
                out.append(line)
                continue

            frame = stack[-1]
            if directive == 'elif':
                if not frame['known']:
                    frame['active'] = frame['parent']
                    if frame['active']:
                        out.append(line)
                    else:
                        changed = True
                    continue

                cond = StripedExport._eval_if_expr(tail, enabled_configs)
                if cond is None:
                    # If this chain becomes unknown mid-way, keep the rest
                    # unchanged to avoid semantic risks.
                    return text, False, 0
                frame['active'] = (frame['parent'] and
                                   (not frame['taken']) and cond)
                frame['taken'] = frame['taken'] or bool(cond)
                changed = True
                continue

            if directive == 'else':
                if not frame['known']:
                    frame['active'] = frame['parent']
                    if frame['active']:
                        out.append(line)
                    else:
                        changed = True
                    continue

                frame['active'] = frame['parent'] and (not frame['taken'])
                frame['taken'] = True
                changed = True
                continue

            # endif
            if frame['preserve']:
                if frame['parent']:
                    out.append(line)
                else:
                    changed = True
            else:
                changed = True
            stack.pop()

        if stack:
            return text, False, 0

        new_text = ''.join(out)
        if new_text != text:
            changed = True
        removed_cnt = text.count('\n') - new_text.count('\n')
        return new_text, changed, max(0, removed_cnt)

    @staticmethod
    def _eval_ifdef(tail, enabled_configs):
        m = re.match(r'^(CONFIG_[A-Za-z0-9_]+)$', tail)
        if not m:
            return None
        return m.group(1) in enabled_configs

    @staticmethod
    def _eval_ifndef(tail, enabled_configs):
        m = re.match(r'^(CONFIG_[A-Za-z0-9_]+)$', tail)
        if not m:
            return None
        return m.group(1) not in enabled_configs

    @staticmethod
    def _eval_if_expr(expr, enabled_configs):
        # Strip trailing // comments to avoid parser confusion.
        expr = re.sub(r'//.*$', '', expr).strip()
        # Keep conservative: reject comparisons/arithmetic.
        if any(op in expr for op in ('==', '!=', '<', '>', '+', '-', '*', '/', '%')):
            return None

        def repl_defined(m):
            name = m.group(1)
            return '1' if name in enabled_configs else '0'

        expr = re.sub(r'defined\s*\(\s*(CONFIG_[A-Za-z0-9_]+)\s*\)',
                      repl_defined, expr)
        expr = re.sub(r'\b(CONFIG_[A-Za-z0-9_]+)\b',
                      lambda m: '1' if m.group(1) in enabled_configs else '0',
                      expr)

        # Unknown identifiers remain -> cannot evaluate safely.
        if re.search(r'\b[A-Za-z_][A-Za-z0-9_]*\b', expr):
            return None

        expr = expr.replace('&&', ' and ')
        expr = expr.replace('||', ' or ')
        expr = re.sub(r'!(?!=)', ' not ', expr)

        if re.search(r'[^\s\d()andortnot]', expr):
            return None

        try:
            val = eval(expr, {'__builtins__': None}, {})
        except Exception:
            return None
        return bool(val)

    # ── phase 7: rewrite ninja files ────────────────────────────────────

    def _rewrite_ninja_files(self, build_dir, path_mapping, output_dir,
                             cmake_vars):
        build_dir_p = Path(build_dir)
        # Rewrite rules.ninja
        rules_src = build_dir_p / 'CMakeFiles' / 'rules.ninja'
        rules_dst = output_dir / 'CMakeFiles' / 'rules.ninja.in'
        rules_dst.parent.mkdir(parents=True, exist_ok=True)

        rules_text = rules_src.read_text()
        rules_text = self._apply_rules_substitutions(rules_text, cmake_vars)
        rules_dst.write_text(rules_text)

        # Rewrite build.ninja
        ninja_src = build_dir_p / 'build.ninja'
        ninja_dst = output_dir / 'build.ninja.in'

        ninja_text = ninja_src.read_text()
        ninja_text = self._apply_build_ninja_substitutions(
            ninja_text, path_mapping, cmake_vars)
        ninja_dst.write_text(ninja_text)

    def _apply_rules_substitutions(self, text, cmake_vars):
        '''Rewrite rules.ninja: replace absolute tool paths with
        @@PLACEHOLDERS@@, strip ccache, remove RERUN_CMAKE rule.'''

        sdk_dir = cmake_vars.get('ZEPHYR_SDK_INSTALL_DIR', '')

        # Strip ccache prefix from commands
        text = re.sub(r'\bccache\s+', '', text)

        # Replace absolute compiler/tool paths with @@...@@
        compiler = cmake_vars.get('CMAKE_C_COMPILER', '')
        if compiler:
            text = text.replace(compiler, '@@CC@@')

        ar = cmake_vars.get('CMAKE_AR', '')
        if ar:
            text = text.replace(ar, '@@AR@@')

        ranlib = cmake_vars.get('CMAKE_RANLIB', '')
        if ranlib:
            text = text.replace(ranlib, '@@RANLIB@@')

        objcopy = cmake_vars.get('CMAKE_OBJCOPY', '')
        if objcopy:
            text = text.replace(objcopy, '@@OBJCOPY@@')

        objdump = cmake_vars.get('CMAKE_OBJDUMP', '')
        if objdump:
            text = text.replace(objdump, '@@OBJDUMP@@')

        readelf = cmake_vars.get('CMAKE_READELF', '')
        if readelf:
            text = text.replace(readelf, '@@READELF@@')

        size = cmake_vars.get('CMAKE_SIZE', '')
        if size:
            text = text.replace(size, '@@SIZE@@')

        # Replace any remaining SDK dir references
        if sdk_dir:
            text = text.replace(sdk_dir, '@@SDK_DIR@@')

        # Replace cmake path
        text = re.sub(r'/usr/bin/cmake\b', 'cmake', text)

        # Remove RERUN_CMAKE and CLEAN_ADDITIONAL rules entirely
        text = self._remove_ninja_rule(text, 'RERUN_CMAKE')
        text = self._remove_ninja_rule(text, 'CLEAN_ADDITIONAL')
        text = self._remove_ninja_rule(text, 'CLEAN')
        text = self._remove_ninja_rule(text, 'HELP')

        return text

    def _apply_build_ninja_substitutions(self, text, path_mapping,
                                         cmake_vars):
        '''Rewrite build.ninja: apply path substitutions, remove
        CUSTOM_COMMAND blocks, remove cmake regeneration targets.'''

        lines = text.split('\n')
        out_lines = []
        skip_block = False
        prev_line_continued = False  # Track ninja $ line continuation
        i = 0

        while i < len(lines):
            line = lines[i]

            # Detect CUSTOM_COMMAND build statements
            if self._is_custom_command_build(line, lines, i):
                skip_block = True
                prev_line_continued = line.rstrip().endswith('$')
                i += 1
                continue

            # Detect cmake regeneration / housekeeping build rules
            if self._is_removed_rule_build(line):
                skip_block = True
                prev_line_continued = line.rstrip().endswith('$')
                i += 1
                continue

            # Skip indented continuation of a skipped block
            if skip_block:
                # A line is part of the block if:
                # 1. The previous line ended with $ (ninja continuation), OR
                # 2. The line is indented (part of a ninja variable block)
                is_continuation = (prev_line_continued or
                                   line.startswith(' ') or
                                   line.startswith('\t'))
                if line == '' or not is_continuation:
                    skip_block = False
                    prev_line_continued = False
                    # Don't skip this line - reprocess it
                    continue
                else:
                    prev_line_continued = line.rstrip().endswith('$')
                    i += 1
                    continue

            # Apply path substitutions to the line
            line = self._substitute_paths(line, path_mapping)

            out_lines.append(line)
            i += 1

        result = '\n'.join(out_lines)

        # Replace cmake_ninja_workdir to be relative
        result = re.sub(
            r'cmake_ninja_workdir\s*=\s*\S+',
            'cmake_ninja_workdir = ./',
            result)

        # Remove | ${cmake_ninja_workdir}... implicit-output sections.
        # When workdir = ./, these become duplicates of the primary
        # outputs and cause "multiple rules" errors in ninja.
        # Match: " | ${cmake_ninja_workdir}path1 ${cmake_ninja_workdir}path2..."
        # before the ": rule" part of a build statement.
        def _strip_implicit_outputs(m):
            # Keep everything except the implicit outputs
            build_part = m.group(1)
            rule_part = m.group(3)
            return build_part + ': ' + rule_part
        result = re.sub(
            r'(build\s+[^\n|:]+?)\s*\|([^\n:]*\$\{cmake_ninja_workdir\}[^\n:]*?):\s*(\S+)',
            _strip_implicit_outputs,
            result)

        # Clean up python/venv references in POST_BUILD
        venv_paths = cmake_vars.get('WEST_TOPDIR', '')
        if venv_paths:
            venv_bin = os.path.join(venv_paths, '.venv/bin/python3')
            result = result.replace(venv_bin, 'python3')

        # Remove check_init_priorities.py invocations from POST_BUILD.
        # The interpreter may be python3 or a venv-local python path.
        result = re.sub(
            r'\s*&&\s*\S*python\S*\s+\S*check_init_priorities\.py\b[^\n]*',
            '', result)

        # Replace remaining /usr/bin/cmake references
        result = re.sub(r'/usr/bin/cmake\b', 'cmake', result)

        return result

    @staticmethod
    def _is_custom_command_build(line, lines, idx):
        '''Check if this line starts a CUSTOM_COMMAND build statement.'''
        stripped = line.strip()
        if not stripped.startswith('build '):
            return False
        # Build statements can span multiple lines via $ continuation,
        # but the rule appears on the first (possibly continued) line.
        # Collect the full logical line.
        full = stripped
        j = idx
        while full.endswith('$'):
            j += 1
            if j < len(lines):
                full += lines[j].strip()
        return ': CUSTOM_COMMAND' in full

    # Rules removed from rules.ninja that should also have their
    # build statements removed from build.ninja.
    _REMOVED_RULES = {'CLEAN', 'HELP', 'RERUN_CMAKE', 'CLEAN_ADDITIONAL'}

    @classmethod
    def _is_removed_rule_build(cls, line):
        '''Check if line is a build statement using a removed rule.'''
        s = line.strip()
        if not s.startswith('build '):
            return False
        # Extract rule name: "build targets: RULE deps"
        colon = s.find(':')
        if colon < 0:
            return False
        after = s[colon + 1:].strip().split()
        if after and after[0] in cls._REMOVED_RULES:
            return True
        return False

    @staticmethod
    def _substitute_paths(line, path_mapping):
        '''Replace absolute paths in a ninja line with mapped relatives.'''
        for prefix, replacement in path_mapping:
            if prefix in line:
                if replacement == '@@SDK_DIR@@':
                    line = line.replace(prefix, '@@SDK_DIR@@')
                elif replacement == '.':
                    line = line.replace(prefix + '/', '')
                    line = line.replace(prefix, '.')
                else:
                    line = line.replace(prefix + '/', replacement + '/')
                    line = line.replace(prefix, replacement)
        return line

    @staticmethod
    def _remove_ninja_rule(text, rule_name):
        '''Remove a ninja rule block (rule NAME\\n  ...lines...).'''
        pattern = re.compile(
            r'#+\n# Rule for [^\n]*\n\nrule\s+' + re.escape(rule_name) +
            r'\n(?:  [^\n]*\n)*',
            re.MULTILINE)
        text = pattern.sub('', text)
        # Also try without the comment header
        pattern2 = re.compile(
            r'rule\s+' + re.escape(rule_name) + r'\n(?:  [^\n]*\n)*',
            re.MULTILINE)
        text = pattern2.sub('', text)
        return text

    # ── phase 8: generate toolchain.env ─────────────────────────────────

    @staticmethod
    def _generate_toolchain_env(cmake_vars, output_dir):
        sdk_dir = cmake_vars.get('ZEPHYR_SDK_INSTALL_DIR',
                                 '/opt/zephyr-sdk')
        compiler = cmake_vars.get('CMAKE_C_COMPILER', '')

        # Try to derive CROSS_COMPILE from the compiler path
        # e.g. /path/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
        #  → CROSS_COMPILE = arm-zephyr-eabi-
        #  → CROSS_COMPILE_TRIPLET = arm-zephyr-eabi
        cross_compile = ''
        triplet = ''
        if compiler:
            basename = os.path.basename(compiler)
            # strip -gcc / -g++ suffix
            m = re.match(r'^(.*)-gcc$', basename)
            if m:
                cross_compile = m.group(1) + '-'
                triplet = m.group(1)

        env_text = f'''\
#!/bin/bash
# Toolchain environment for the exported Zephyr project.
# Adjust ZEPHYR_SDK_INSTALL_DIR to match your local SDK installation.

export ZEPHYR_SDK_INSTALL_DIR="{sdk_dir}"

CROSS_COMPILE="{cross_compile}"
export CROSS_COMPILE_TRIPLET="{triplet}"

export PATH="${{ZEPHYR_SDK_INSTALL_DIR}}/${{CROSS_COMPILE_TRIPLET}}/bin:${{PATH}}"

echo "Toolchain configured: ${{CROSS_COMPILE_TRIPLET}}"
echo "  SDK: ${{ZEPHYR_SDK_INSTALL_DIR}}"
echo "  CC : $(which ${{CROSS_COMPILE}}gcc 2>/dev/null || echo 'NOT FOUND')"
'''
        (output_dir / 'toolchain.env').write_text(env_text)

    # ── phase 9: generate setup.py ──────────────────────────────────────

    @staticmethod
    def _generate_setup_py(output_dir):
        setup_text = r'''#!/usr/bin/env python3
"""Resolve @@PLACEHOLDER@@ tokens in .ninja.in template files.

Reads environment variable ZEPHYR_SDK_INSTALL_DIR (must be set before
running, typically via  `source toolchain.env`).

This script produces build.ninja and CMakeFiles/rules.ninja from their
.in templates.
"""
import os, re, sys, shutil, pathlib

def main():
    sdk = os.environ.get('ZEPHYR_SDK_INSTALL_DIR')
    if not sdk:
        print('ERROR: ZEPHYR_SDK_INSTALL_DIR is not set. '
              'Run `source toolchain.env` first.', file=sys.stderr)
        sys.exit(1)

    sdk = sdk.rstrip('/')

    # Discover cross-compile triplet from environment (set by toolchain.env)
    triplet = os.environ.get('CROSS_COMPILE_TRIPLET')
    if not triplet:
        print('ERROR: CROSS_COMPILE_TRIPLET is not set. '
              'Run `source toolchain.env` first.', file=sys.stderr)
        sys.exit(1)

    cross = triplet + '-'
    bindir = os.path.join(sdk, triplet, 'bin')
    cc  = os.path.join(bindir, cross + 'gcc')
    ar  = os.path.join(bindir, cross + 'ar')
    ranlib = os.path.join(bindir, cross + 'ranlib')
    objcopy = os.path.join(bindir, cross + 'objcopy')
    objdump = os.path.join(bindir, cross + 'objdump')
    readelf = os.path.join(bindir, cross + 'readelf')
    size    = os.path.join(bindir, cross + 'size')

    replacements = {
        '@@SDK_DIR@@': sdk,
        '@@CC@@':      cc,
        '@@AR@@':      ar,
        '@@RANLIB@@':  ranlib,
        '@@OBJCOPY@@': objcopy,
        '@@OBJDUMP@@': objdump,
        '@@READELF@@': readelf,
        '@@SIZE@@':    size,
    }

    templates = list(pathlib.Path('.').rglob('*.ninja.in'))
    for tmpl in templates:
        text = tmpl.read_text()
        for placeholder, value in replacements.items():
            text = text.replace(placeholder, value)
        out = tmpl.with_suffix('')  # strip .in
        out.write_text(text)
        print(f'  {tmpl} → {out}')

    print('Setup complete – run  ninja  to build.')

if __name__ == '__main__':
    main()
'''
        (output_dir / 'setup.py').write_text(setup_text)

    # ── phase 10: generate Makefile ─────────────────────────────────────

    @staticmethod
    def _generate_makefile(output_dir):
        makefile_text = '''\
# Exported Zephyr build – portable Makefile wrapper.
#
# Usage:
#   source toolchain.env   # configure SDK path (once per shell)
#   make                   # build
#   make clean             # clean object files
#
.PHONY: all clean setup

all: setup
\tninja

setup: build.ninja

build.ninja: build.ninja.in CMakeFiles/rules.ninja.in toolchain.env
\tpython3 setup.py

clean:
\tninja -t clean 2>/dev/null; rm -f build.ninja CMakeFiles/rules.ninja
'''
        (output_dir / 'Makefile').write_text(makefile_text)
