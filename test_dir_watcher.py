"""
test_dir_watcher.py

"""
import itertools
import logging
import os
import os.path
import random
import re
import shutil
import signal
import subprocess
import sys
import time

import simplejson

class ConfigurationError(Exception):
    pass

_pandora_dir = None
_config_path = None
_exclude_path = None
_notify_dir = None
_data_root = None
_out_of_bounds_dir = None
_dir_watcher_process = None
_top_level_directories = list()
_exclude_directories = list()
_original_directories = list()
_new_directories = list()
_trees = dict()
_file_name_template = "file%08d_%s"
_file_name_re = re.compile("^file(?P<file_number>\d{8})_.*$")
_dir_name_template = "dir%08d_%s"
_dir_name_re = re.compile("^dir(?P<dir_number>\d{8})_.*$")
_notification_name_re = re.compile("^.*\d{8}.txt$")

def _parse_command_line():
    from optparse import OptionParser

    parser = OptionParser()
    parser.add_option(
        '-f', "--log-file", dest="log_file", type="string",
        help="path of the log file"
    )
    parser.set_defaults(log_file="/var/log/pandora/test_dir_watcher.log")
    parser.add_option(
        '-l', "--log-level", dest="log_level", type="string",
        help="set the root log level"
    )
    parser.set_defaults(log_level="debug")
    parser.add_option(
        '-t', "--test-definition", dest="test_definition_file", type="string",
        help="path to json test definition file"
    )

    options, _ = parser.parse_args()

    if options.test_definition_file is None:
        raise ConfigurationError("You must specify a test definition file")

    options.log_level = options.log_level.upper()

    return options

def _initialize_logging(options):
    """initialize the log"""
    
    if not options.log_level in logging._levelNames:
        raise ConfigurationError("Unknown log level: %s" % options.log_level)
    log_level = logging._levelNames[options.log_level]      
    
    logging.basicConfig(
        level=log_level,
        format='%(asctime)s %(levelname)-8s %(name)-20s: %(message)s',
        filename=unicode(options.log_file),
        filemode='w'
    )
    
def _load_test_definition(options):
    """read the test_definition file and convert from json"""
    log = logging.getLogger("_load_config")
    log.info("Loading %s" % (options.test_definition_file, ))
    input_file = open(unicode(options.test_definition_file), "rb")
    input_text = input_file.read()
    input_file.close()
    return simplejson.loads(input_text)
    
def _create_directories(test_definition):
    global _pandora_dir
    global _config_path
    global _exclude_path
    global _notify_dir
    global _data_root
    global _out_of_bounds_dir

    test_path = os.path.abspath(test_definition["test_directory"])

    _pandora_dir = "%s/pandora" % (test_path, )
    _config_path = "%s/config.txt" % (_pandora_dir, )
    _exclude_path = "%s/exclude.txt" % (_pandora_dir, )
    _notify_dir = "%s/fs_notify" % (_pandora_dir, )
    _data_root = "%s/data" % (test_path, )
    _out_of_bounds_dir = "%s/out_of_bounds" % (_data_root, )

    if os.path.exists(test_path):
        shutil.rmtree(test_path)
    os.mkdir(test_path)
    os.mkdir(_pandora_dir)
    os.mkdir(_notify_dir)
    os.mkdir(_data_root)
    os.mkdir(_out_of_bounds_dir)

def _create_tree(parent_path, dir_prefix, height, width, accum, level=""):
    # recursively build a tree of directories like
    #     0          1
    #    / \        / \
    #   00 01     10  11
    if len(level) < height:
        for x in xrange(width):
            new_level = "".join([level, str(x), ])
            path = os.path.join(
                parent_path, "".join([dir_prefix, "_", new_level, ])
            )
            os.mkdir(path)
            accum.append(path)
            _create_tree(path, dir_prefix, height, width, accum, new_level)

def _prepare_test(test_definition):
    for top_level_directory in test_definition["top_level_directories"]:
        if top_level_directory["name"] == "pandora":
            path = _pandora_dir
        else:
            path = os.path.join(_data_root, top_level_directory["name"])
            os.mkdir(path)
            _create_tree(
                path, 
                top_level_directory["name"], 
                top_level_directory["height"],
                top_level_directory["width"],
                _original_directories
            )
        _top_level_directories.append(path)

    exclude_file = open(_exclude_path, "w")
    # always exclude the pandora dir
    exclude_file.write("%s\n" % (_pandora_dir, ))
    for exclude_directory in test_definition["exclude_directories"]:
        if exclude_directory == "random":
            exclude_path = random.choice(_top_level_directories)
            exclude_file.write("%s\n" % (exclude_path, ))

    exclude_file.close()

    config_file = open(_config_path, "w")
    for top_level_dir in _top_level_directories:
        config_file.write("%s\n" % (top_level_dir, ))
    config_file.close()

def _start_dir_watcher(executable_path):
    global _dir_watcher_process

    args = [
        executable_path,
        str(os.getpid()),
        _config_path,
        _exclude_path,
        _notify_dir
    ]
    _dir_watcher_process = subprocess.Popen(args)
    assert _dir_watcher_process.returncode is None, \
        _dir_watcher_process.returncode

def _stop_dir_watcher():
    log = logging.getLogger("_stop_dir_watcher")
    assert _dir_watcher_process is not None
    _dir_watcher_process.poll()
    if _dir_watcher_process.returncode is None:
        os.kill(_dir_watcher_process.pid, signal.SIGTERM)
    else:
        log.error("Already stopped %s" % (_dir_watcher_process.returncode, ))
    _dir_watcher_process.wait()
    print "dir watcher terminated code", _dir_watcher_process.returncode

def _list_file_numbers_in_directory(dir_path):
    """list the files as a sorted list of ints conveted from file000"""
    file_numbers = list()

    for entry_name in os.listdir(dir_path):
        match_object = _file_name_re.match(entry_name)
        if match_object is not None:
            file_numbers.append(int(match_object.group("file_number")))

    file_numbers.sort()
    return file_numbers
    
def _list_dir_numbers_in_directory(dir_path):
    """list the directories as a sorted list of ints conveted from dir000"""
    dir_numbers = list()

    for entry_name in os.listdir(dir_path):
        match_object = _dir_name_re.match(entry_name)
        if match_object is not None:
            dir_numbers.append(int(match_object.group("dir_number")))

    dir_numbers.sort()
    return dir_numbers
    
def _add_file(target_directory):
    file_numbers = _list_file_numbers_in_directory(target_directory)
    prev_number = (0 if len(file_numbers) == 0 else file_numbers[-1])

    # create a file name of random size between 13 and 255
    file_path = os.path.join(
        target_directory, 
        _file_name_template % (prev_number+1, "x" * random.randint(0, 242), )
    )
    output_file = open(file_path, "w")
    output_file.close()
    
def _add_file_test(_):
    """add a file under every existing directory"""
    expected_results = list()
    for test_directory in itertools.chain(
        _original_directories, _new_directories
    ):
        expected_results.append(test_directory)
        _add_file(test_directory)

    return set(expected_results)

def _delete_one_file(target_directory):
    for entry in os.listdir(target_directory):
        path = os.path.join(target_directory, entry)
        if os.path.isfile(path):
            os.unlink(path)
            return True
    return False

def _delete_file_test(_):
    """delete one file under every existing directory"""
    expected_results = list()
    for test_directory in itertools.chain(
        _original_directories, _new_directories
    ):
        if _delete_one_file(test_directory):
            expected_results.append(test_directory)

    return set(expected_results)

def _add_dir(target_directory):
    dir_numbers = _list_dir_numbers_in_directory(target_directory)
    prev_number = (0 if len(dir_numbers) == 0 else dir_numbers[-1])

    # create a file name of random size between 12 and 255
    dir_path = os.path.join(
        target_directory, 
        _dir_name_template % (prev_number+1, "x" * random.randint(0, 243), )
    )

    os.mkdir(dir_path)

    return dir_path
    
def _add_directory_test(_):
    """
    add a directory under every existing directory,
    """
    expected_results = list()
    work_list = list()
    for test_directory in itertools.chain(
        _original_directories, _new_directories
    ):
        expected_results.append(test_directory)
        new_directory = _add_dir(test_directory)
        work_list.append(new_directory)

    _new_directories.extend(work_list)

    return set(expected_results)

def _delete_directory_test(_):
    """delete one new directory under every existing directory"""
    expected_results = list()
    deleted_directories = list()

    for top_level_dir in _top_level_directories:
        for base_dir, dirlist, _ in os.walk(top_level_dir, topdown=False):
            for dir_name in dirlist:
                if _dir_name_re.search(dir_name):
                    target_directory = os.path.join(base_dir, dir_name)
                    shutil.rmtree(target_directory)
                    deleted_directories.append(target_directory)
                    # if we delete a directory, the event appears in
                    # the parent directory
                    expected_results.append(base_dir)

    for deleted_directory in deleted_directories:
        _new_directories.remove(deleted_directory)

    return set(expected_results)

def _select_random_file(directories):
    source_directory = None
    file_name = None
    while source_directory is None:
        source_directory = random.choice(directories)
        for entry in os.listdir(source_directory):
            if _file_name_re.search(entry) is not None:
                file_name = entry
                break

    return source_directory, file_name

def _move_file_test(_):
    expected_results = list()

    # move a file out of bounds
    source_directory, file_name = _select_random_file(_original_directories)
    source_path = os.path.join(source_directory, file_name)

    dest_path = os.path.join(_out_of_bounds_dir, file_name)
    os.rename(source_path, dest_path)
    expected_results.append(source_directory)

    # move a file from one watched folder to another
    source_directory, file_name = _select_random_file(_original_directories)
    source_path = os.path.join(source_directory, file_name)

    dest_directory = source_directory
    while dest_directory == source_directory:
        dest_directory = random.choice(_original_directories)

    dest_path = os.path.join(dest_directory, file_name)
    os.rename(source_path, dest_path)
    expected_results.extend([source_directory, dest_directory, ])

    return set(expected_results)

def _add_tree_test(test):
    log = logging.getLogger("_add_tree_test")
    expected_results = list()
    
    target_directory = random.choice(_original_directories)
    expected_results.append(target_directory)

    path = os.path.join(target_directory, test["tree_name"])
    os.mkdir(path)

    tree_child_paths = list()

    log.info("adding tree %s (%s, %s) at %s" % (
        test["tree_name"], 
        test["height"],
        test["width"],
        target_directory, 
    ))
    _create_tree(
        path, 
        test["tree_name"], 
        test["height"],
        test["width"],
        tree_child_paths
    )

    _trees[test["tree_name"]] = (path, tree_child_paths, )

    return set(expected_results)

def _remove_tree_test(test):
    expected_results = list()
    
    target_directory, tree_child_paths = _trees[test["tree_name"]]

    expected_results.append(os.path.dirname(target_directory))
    expected_results.extend([os.path.dirname(x) for x in tree_child_paths])

    shutil.rmtree(target_directory)

    del _trees[test["tree_name"]]

    return set(expected_results)

def _move_tree_test(test):
    expected_results = list()
    
    source_directory, _ = _trees[test["tree_name"]]
    source_parent = os.path.dirname(source_directory)

    expected_results.append(source_parent)

    dest_parent = source_parent
    while dest_parent == source_parent:
        dest_parent = random.choice(_original_directories)
    expected_results.append(dest_parent)

    dest_directory = os.path.join(dest_parent, test["tree_name"])

    assert source_directory != dest_directory, (
        source_directory, dest_directory
    )

    os.rename(source_directory, dest_directory)

    return set(expected_results)

def _get_notifications():
    raw_notifications = list()

    for notification_name in os.listdir(_notify_dir):

        if _notification_name_re.search(notification_name) is None:
            continue

        notification_path = os.path.join( _notify_dir, notification_name)
        notification_file = open(notification_path, "r")
        for notification_line in notification_file:
            raw_notifications.append(notification_line.strip())
        notification_file.close()

        os.remove(notification_path)

    # weed out re-notificatons of lower level dirs
    notifications = list()
    
    raw_notifications.sort(key=len)

    while len(raw_notifications) > 0:

        head = raw_notifications[0]
        notifications.append(head)
        head += "/"

        raw_notifications = raw_notifications[1:]

        dups = [r for r in raw_notifications if r.startswith(head)]
        raw_notifications = [r for r in raw_notifications if r not in dups]

    return set(notifications)

def _check_results(expected_results, verbose):
    _dir_watcher_process.poll()
    assert _dir_watcher_process.returncode is None, \
        _dir_watcher_process.returncode

    notifications = _get_notifications()

    expected_results = list(expected_results)
    expected_results.sort()
    notifications = list(notifications)
    notifications.sort()

    short_expected_results = map(os.path.basename, expected_results)
    short_notifications = map(os.path.basename, notifications)
    # make the lists the same size, because we don't have itertools.zip_longest
    while len(short_expected_results) < len(short_notifications):
        short_expected_results.append("*none*")
    while len(short_notifications) < len(short_expected_results):
        short_notifications.append("*none*")

    max_len = max(map(len, short_notifications))

    print
    print "%s notifications, %s expected" % (
        len(notifications), len(expected_results),
    )
    if verbose:
        for n, e in zip(short_notifications, short_expected_results):
            print "%-*s - %s" % (max_len, n, e)

    assert len(notifications) == len(expected_results), (
        len(notifications), len(expected_results)
    )

    match_count = 0
    for n, e in zip(notifications, expected_results):
        if n != e:
            print "notification %s != %s after %s matches" % (
                n, e, match_count
            )
            sys.exit(-1)
        match_count += 1

_test_dispatch_table = {
    "add_file"          : _add_file_test,
    "delete_file"       : _delete_file_test,
    "move_file"         : _move_file_test,
    "add_directory"     : _add_directory_test,
    "delete_directory"  : _delete_directory_test,
    "add_tree"          : _add_tree_test,
    "remove_tree"       : _remove_tree_test,
    "move_tree"         : _move_tree_test,
}

def _run_test(test, verbose):
    log = logging.getLogger(test["name"])
    log.info("start test")

    test_function = _test_dispatch_table[test["name"]]
    expected_results = test_function(test)

    print test["name"], "press [enter] to continue"
    raw_input()

    _check_results(expected_results, verbose)

    log.info("end test")

def main():
    """main entry point""" 
    options = _parse_command_line()
    _initialize_logging(options)
    log = logging.getLogger("main")
    log.info("program starts")

    test_definition = _load_test_definition(options)
    _create_directories(test_definition)
    _prepare_test(test_definition)
    _start_dir_watcher(test_definition["executable_path"])

    print
    print "dir watcher process is", _dir_watcher_process.pid
    print "attach debugger if you want to, then press [enter]"
    raw_input()

    for test in test_definition["tests"]:
        assert _dir_watcher_process.returncode is None, \
            _dir_watcher_process.returncode

        _run_test(test, test_definition["verbose"])

    _stop_dir_watcher()

    log.info("program terminates normally")
    return 0

if __name__ == "__main__":
    sys.exit(main())
