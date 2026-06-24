# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

config LOG_BACKEND_FS
	bool "File system backend"
	depends on FILE_SYSTEM
	select LOG_OUTPUT
	select LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP
	help
	  When enabled, backend is using the configured file system to output logs.
	  As the file system must be mounted for the logging to work, it must be
	  either configured for auto-mount or manually mounted by the application.
	  Log messages are discarded as long as the file system is not mounted.

if LOG_BACKEND_FS

backend = FS
backend-str = fs
source "subsys/logging/Kconfig.template.log_format_config"

config LOG_BACKEND_FS_AUTOSTART
	bool "Automatically start fs backend"
	default y
	help
	  When enabled automatically start the file system backend on
	  application start.

config LOG_BACKEND_FS_OVERWRITE
	bool "Old log files overwrite"
	default y
	help
	  When enabled backend overwrites oldest log files.
	  In other case, when memory is full, new messages are dropped.

config LOG_BACKEND_FS_APPEND_TO_NEWEST_FILE
	bool "Append to the newest log file"
	default y
	help
	  When enabled and when there is space left in the newest log file,
	  backend appends to it.
	  When disabled backend creates a new log file on every startup.

config LOG_BACKEND_FS_FILE_PREFIX
	string "Log file name prefix"
	default "log."
	help
	  User defined name of log files saved in the file system.
	  The prefix is followed by the number of log file.

config LOG_BACKEND_FS_DIR
	string "Log directory"
	default "/lfs1"
	help
	  Directory to which log files will be written.

config LOG_BACKEND_FS_FILE_SIZE
	int "User defined log file size"
	default 4096
	range 128 1073741824
	help
	  Max log file size (in bytes).

config LOG_BACKEND_FS_FILES_LIMIT
	int "Max number of files containing logs"
	default 10
	help
	  Limit of number of files with logs. It is also limited by
	  size of file system partition.

config LOG_BACKEND_FS_DEGRADE_FAILURE_THRESHOLD
	int "Consecutive failures before fs backend degrades"
	default 3
	range 1 255
	help
	  Number of consecutive file write path failures tolerated before
	  the file system backend marks itself degraded and deactivates.

config LOG_BACKEND_FS_DEGRADE_PRINTK_INTERVAL_MS
	int "FS backend degraded printk interval in milliseconds"
	default 0
	range 0 2147483647
	help
	  Period for printk warnings after the file system backend degrades.
	  Set to 0 to disable periodic warnings.

endif # LOG_BACKEND_FS
