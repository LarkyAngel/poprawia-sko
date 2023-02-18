from argparse import ArgumentParser
import subprocess
import re
import time


def test0(path):
    """Test from description called from within python, without checks."""
    with open("cmd.txt") as f:
        subprocess.run(path, stdin=f, stderr=subprocess.DEVNULL, text=True, timeout=1, start_new_session=True)


def test1(path):
    """Test from description called from within python, without checks."""
    process = subprocess.Popen(path, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               text=True, start_new_session=True)
    process.communicate("run cat in.txt\nsleep 200\nout 0", timeout=1)


def print_and_pass(s):
    print(s, end="")
    return s


def get_process(path):
    return subprocess.Popen(
        ["stdbuf", "-oL", path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        universal_newlines=True,
        bufsize=1,
        start_new_session=True)


def test2(path):
    """Test from description called from within python, with checks after EOF."""
    process = get_process(path)
    process.stdin.write("run cat in.txt\nsleep 100\nout 0")
    process.stdin.close()
    assert re.match("Task 0 started: pid \\d+.", print_and_pass(process.stdout.readline()))
    assert re.match("Task 0 ended: status 0.", print_and_pass(process.stdout.readline()))
    assert re.match("Task 0 stdout: 'bar'.", print_and_pass(process.stdout.readline()))


def test3(path):
    """Tests that executor prints output not only after getting EOF."""
    process = get_process(path)
    process.stdin.write("run cat in.txt\n")
    process.stdin.write("sleep 100\n")
    assert re.match("Task 0 started: pid \\d+.", print_and_pass(process.stdout.readline()))
    assert re.match("Task 0 ended: status 0.", print_and_pass(process.stdout.readline()))
    process.stdin.write("out 0")
    process.stdin.close()
    assert re.match("Task 0 stdout: 'bar'.", print_and_pass(process.stdout.readline()))


def test4(path):
    """Tests that 'sleep' works."""
    process = get_process(path)
    start = time.time()
    process.communicate("sleep 1000")
    end = time.time()
    assert end - start > 0.9
    assert end - start < 1.2


def test5(path) -> None:
    # Not obligatory.
    """Tests that 'signalled' info is of expected form, and that 'kill' works."""
    process = get_process(path)
    start = time.time()
    stdout, _ = process.communicate("run sleep 2\nkill 0")
    end = time.time()
    print(stdout, end="")
    assert re.match("^Task 0 started: pid \\d+\\.\nTask 0 ended: signalled\\.$", stdout)
    assert end - start < 1


def test6(path) -> None:
    """
    Tests that executor don't SIGINT itself during kill.

    If it fails, you probably call 'kill()' with task_id in place of pid.
    Kill(0) kills the whole processes group, failing many tests.
    """
    process = subprocess.Popen(
        ["stdbuf", "-oL", path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        universal_newlines=True,
        bufsize=1)
    try:
        stdout, _ = process.communicate("run sleep 2\nkill 0")
    except KeyboardInterrupt:
        assert False


def main():
    parser = ArgumentParser()
    parser.add_argument("path", help="Path do executor executable")

    args = parser.parse_args()
    path = args.path

    print(
        "The first two tests should be checked visually"
        " (against output from the task description)."
    )
    print("====================Test0====================", flush=True)
    test0(path)
    print("====================Test1====================", flush=True)
    test1(path)
    print("====================Test2====================")
    test2(path)
    print("====================Test3====================")
    test3(path)
    print("====================Test4====================")
    test4(path)
    print("====================Test5====================")
    test5(path)
    print("====================Test6====================")
    test6(path)


if __name__ == "__main__":
    main()
