"""
A program to launch a filesystem watcher and track its progress
"""
import os
import subprocess
import sys

_memory_database = "SPIDEROAK_DIR_WATCHER_MEMORY_DATABASE"

def main(executeable_path, config_path, exclude_path, notification_path):
    """launch a filesystem watcher"""
    print "parent pid =", os.getpid()
    args = [
	    executeable_path, 
	    str(os.getpid()), 
	    config_path, 
        exclude_path,
	    notification_path, 
    ]
    environment = dict()
    if _memory_database in os.environ:
        environment[_memory_database] = os.environ[_memory_database]

    process = subprocess.Popen(args, env=environment)
    print "process started pid =", process.pid
    process.wait()
    print "process terminates", process.returncode

if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) != 4:
        print "invalid commandline %s" % (args, )
        print "Usage: ... <executable path> <config path> <exclude path> <notification_path>"
        sys.exit(-1)

    sys.exit(main(*args))
