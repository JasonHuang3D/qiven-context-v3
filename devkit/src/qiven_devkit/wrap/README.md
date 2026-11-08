# wrap

wrap is the one way devkit starts a foreign program. A wrapper per tool
calls it, and each call has a name: the wrapper's verb, such as
`git ls-files`. A call that does not give its answer raises an error in
wrap's own words that says the call's name and the reason. Each wrapper has
a fake with the same interface, for the tests of the code above it, so that
those tests start no program.

wrap holds no fact of the project it serves. Every program, environment,
directory, limit and expected value comes from the caller. wrap looks up no
default path, climbs no tree and reads no variable of its own process: the
environment a program runs under is the one the caller hands in.

## Requirements

- Windows: a program's tree is ended through a job object. On any other
  system every call refuses, as `Failed` with `status` None naming the
  system, and starts nothing.
- Python's standard library only.

## The call base: `call`

```
call.call(name, program, args, *, env, cwd, limit, data=None, takes=(0,), said="utf-8") -> Answer
```

- `program` is a file path, or a bare name looked up on the `PATH` of `env`.
  A bare name without an extension is tried with each extension in the
  `PATHEXT` of `env`, in order. The current directory is never searched.
  `call.locate(name, program, env)` gives the file the same way.
- `env` is the program's whole environment, a mapping of str to str.
  `cwd` is the directory it starts in. `limit` is in seconds, above 0. All
  three are required.
- Standard input is closed, unless `data` (bytes) is given, which is written
  to it.
- The program starts suspended, with no window. It is put into a job object
  of its own and only then runs, so every process it starts joins that job.
  `subprocess` closes the thread handle it is given, so the program is
  resumed through a snapshot of the system's threads. Only documented
  kernel32 calls are used.
- The limit covers the whole call: until the program has ended and its
  output pipes are closed. A process it started that still holds a pipe
  holds the call. Past the limit, every process in the job is ended, and wrap
  waits up to 10 s for them to be gone.
- When a call ends inside its limit, the job's handle is closed and nothing
  in it is ended: a process the program left running goes on. If the caller
  is stopped by an exception during a call (KeyboardInterrupt included), the
  whole job is ended. If the caller's process is killed, the job is not
  ended.
- A process that asks to break away from the job fails to start, because the
  job does not allow breakaway.

`Answer(name, status, out, err)`: the program's status, and its output as
bytes. It is returned only when `status` is one of `takes`.

### A held program: `start`

```
call.start(name, program, args, *, env, cwd, limit) -> Running
```

For a program the caller talks to while it runs, such as a debugger.
`program`, `env`, `cwd` and `limit` are taken as `call` takes them, and the
program starts as `call` starts one: suspended, with no window, into a job
object of its own, and only then run. Its standard input is a pipe the caller
writes to. Its output and its standard error are one pipe, in the order the
program wrote them. `limit` covers the program's whole life, from its start.

`Running` (`name`, `file`, `pid`, `limit`). `name` is the call name its
errors carry; a wrapper that holds a program for several calls sets it to
the call in hand.

- `send(data)` writes bytes to its standard input. A program that no longer
  reads it (it has ended) is `Failed`, with its status where it has one.
- `read(seconds)` gives the next bytes it wrote, waiting at most `seconds`:
  `b""` where none came in that time, None once its output has ended, which
  is when every process holding the pipe has ended.
- `wait(seconds)` gives its status once it has ended, None where it still
  runs after `seconds`.
- `close()` ends every process still in the job, waits up to 10 s for them to
  be gone, and lets go of the job. A `with` block closes it on leaving,
  whatever ends the block. A held program is ended at its close, unlike a
  `call`'s leftovers: it is held because it does not end by itself. If the
  caller's process is killed, the job is not ended.
- A `send`, `read` or `wait` that reaches the limit ends the whole job and
  raises `Slow`. One made after `close` is a `ValueError`.

### Errors

Each error is a `call.CallError`, whose fields are `call` (the call's name),
`answer` (the word below) and `reason`. Printed, it reads
`<call>: <answer>: <reason>`.

| Error | `answer` | When |
|---|---|---|
| `Missing` | missing | No file at the path the caller gave, no file for the bare name on the `PATH` of `env`, or no `PATH` in `env`. |
| `Slow` | slow | The program started and passed its limit. It is present: its whole tree has been ended. `limit` holds the limit. The reason says how many of its processes were still alive 10 s after they were ended, if any. The output of a program past its limit is not kept. |
| `Failed` | failed | The program ran and ended with a status not in `takes`. `status` holds the status, and the reason holds the program's last lines: up to 20 lines of its standard error, or of its output when its standard error is empty, decoded by `said` with errors replaced. A program that exists but could not be started is also `Failed`, with `status` None and the system's reason. |
| `Unreadable` | unreadable | The program answered, but not in the form the call reads. A wrapper raises it when it parses an answer. |

A wrapper can add errors of its own, as subclasses of `CallError` with an
`answer` word of their own, for an answer that is in the right form but
refuses what was asked (`git.NoCommit`, `setup_env.Lacks`).

An argument that is wrong, such as a limit of 0, an env that is not a
mapping, a `cwd` that is not a directory, or a path that holds a character
the call cannot pass, is a
`ValueError` or a `TypeError` raised before anything starts: it is a bug in
the caller, not an answer.

## Fakes: `fake`

Each wrapper `X` in the module `x` comes with `x.FakeX`, which has `X`'s
methods and starts no program. Every fake is built on `fake.Fake`, in one
form:

```
FakeX(answers)       answers: {call name: answer}
fake.calls           [(call name, {argument: value}), ...], in the order the calls were made
```

When a call is made, the fake records it and then looks up its name in
`answers`:

- a `CallError` instance (or any other exception) is raised;
- a callable is called with the call's arguments by keyword, and what it
  returns is the answer;
- any other value is returned as it is.

A call name that `answers` does not hold raises `fake.Unanswered`. A fake has
no test of its own.

## git: `git`

```
git.Git(program, env, limit)
```

One `Git` is git's wrapper. Its `program`, `env` and `limit` hold for each
of its calls. Each call starts git once, in the folder it is given, which
must be inside a work tree. Every `-z` answer is decoded in one place: the
fields are separated by NUL, the answer ends with a NUL (or is empty), and
each field is UTF-8. Any other answer is `Unreadable`.

| Method | Call name | What it gives |
|---|---|---|
| `listing(folder)` | `git ls-files` | The names of the files under `folder` that git tracks, and of the untracked files that no ignore rule names, relative to `folder`, separated by `/`, in git's order, each named once (`ls-files -z --cached --others --exclude-standard`). A tracked file that is gone from the work tree is still listed. |
| `files_at(folder, commit, paths)` | `git cat-file` | `{path: bytes}` of each path in `paths` (from the repository's root, separated by `/`) that names a file at `commit`. A path that names no file there (nothing at all, or a directory) is left out. If `commit` names no commit, `NoCommit` is raised. One `cat-file --batch`. A path holding a newline is a `ValueError`. |
| `attributes(folder, paths, names)` | `git check-attr` | `{path: {name: value}}` for the attributes `names` of `paths`, which are relative to `folder`. A value is git's own word: `set`, `unset`, `unspecified`, or the value the attribute was given. One `check-attr -z --stdin`. |

`git.NoCommit` (answer `no such commit`): `files_at` was given a commit that
git does not have.

Not here: the content id over the files `listing` names, which is the
later part's. Working out a file's blob form starts no git, so it is not a
wrap of git.

`git.steady_env(base, directory)` gives an environment under which git
reads a work tree the same, whatever the host's git configuration says. It
is `base` without any `GIT_` variable, with git's system configuration and
system attributes turned off. The global configuration, the attributes file
and the excludes file are each set to an empty file, `directory/empty`,
which is written there. `core.fsmonitor` is turned off at command scope,
where no configuration file can override it. Without this, a user's global
ignore file would change what `listing` lists. It needs git 2.31 or later,
for `GIT_CONFIG_COUNT`.

`git.FakeGit(answers)`: the call names above, with the methods' arguments
by keyword (`folder`, `commit`, `paths`, `names`).

## A setup script's environment: `setup_env`

Some tool chains are set up by a script that changes the environment, such
as a batch file run by `cmd` with arguments like a toolset version. Running
it can take a minute and more. `setup_env` captures the environment the
script leaves once per key and keeps it in a directory the caller hands in.
The captures after that read the kept file and start nothing.

```
setup_env.SetupEnvironment(shell, store, limit)
  .environment(script, args, base, carries, inputs=()) -> {name: value}
```

- `shell` is the command interpreter, a path or a bare name on the `PATH`
  of `base`, as `call` takes it. `store` is the directory the kept files go
  into; it is made if it is not there. `limit` is in seconds, for each
  capture.
- `script` is the setup script's path, and `args` its arguments, words
  passed to it as they are. A word that is empty, or holds whitespace or one of
  `" % ! & | < > ^ ( )` is a `ValueError`. So is a script or shell path
  holding `" % !`, or a path or word that the console's OEM code page,
  which the driver is written in, cannot hold.
- `base` is the environment the script runs under, a mapping of str to str.
- `carries` is `{name: pattern}`: each variable the captured environment
  must hold, whose value must match the regular expression in full. Whether
  a setup gives the toolset that was asked for is the caller's fact, handed
  in here. Names are compared without case, as Windows compares them.
- `inputs` are further files whose content enters the key, such as the
  scripts the setup script calls.

A capture runs `shell /d /c .\<driver>.cmd` in `store`. The driver is a
batch file written there for the capture and removed after it. It calls the
script with its arguments, sends the script's output to standard error,
stops with the script's status if that is not 0, and then has a second
`shell /d /u /c set` print the environment the script left, in UTF-16. `/d`
keeps the registry's AutoRun commands out.

**The key** is the SHA-256 of these, as JSON: the shell's file, the script's
absolute path, the SHA-256 of the script's bytes, `args`, the SHA-256 of each
input's bytes by its absolute path, and every name and value of `base`.

**Invalidation.** A change to any one of these gives a new key, and the
next call captures again. A change the key does not see is not noticed:
an update to a file the script reads that the caller did not name in
`inputs`, or a toolset installed or removed. To capture again for such a
change, the caller names the file in `inputs`, or removes the kept file. A
kept file that does not read back (it is not JSON, holds another key, or is
not a mapping of str to str) is captured again and written over. A kept
file is never removed, and the files of old keys stay in `store`.

**What is kept** is the whole environment the script left, not what it
changed against `base`. A setup reads its base: a typical one puts its own
directories before the base's `PATH`. What it leaves depends on the base,
and a set of changes applied to another base would be a guess. That is why
`base` is part of the key: a caller that wants the kept environment to be
used on its next run hands in the same base.

**The kept file** is `store/<key>.json`, holding `key`, `script`, `args` and
`environment`. It is written to a temporary file in `store` first, and that
file then replaces it, so a reader sees the whole old file or the whole new
one. Two captures of the same key at once both write the same content.

`carries` is checked on every answer, whether it was captured or kept. What
a setup leaves is kept even when it does not carry what was asked, because
for the same key it would leave the same again.

Errors: the call base's own, under the call name `setup capture`. The shell
missing, or no file at `script` or at an input, is `Missing`. A capture
past its limit is `Slow`, never `Missing`. A script that ends with a status
other than 0 is `Failed`, with its last lines decoded by the console's OEM
code page. An answer that is not UTF-16, or that holds no variable, is
`Unreadable`. A capture whose environment does not carry what `carries`
asks is `setup_env.Lacks` (answer `lacks`), which names the variable, the
pattern and the value it holds.

`setup_env.FakeSetupEnvironment(answers)`: the call name `setup capture`,
with the arguments of `environment` by keyword.

## CMake: `cmake`

```
cmake.CMake(program, env, limit)
```

One `CMake` is CMake's wrapper: it configures a build tree and reads back
what the tree recorded. Its `program`, `env` and `limit` hold for each of its
calls. Every source and tree it is given is an absolute path, else a
`ValueError`: a relative one would be read from a directory nobody named.

| Method | Call name | What it gives |
|---|---|---|
| `configure(source, tree, *, preset=None, generator=None, entries={})` | `cmake configure` | Configures `tree` from the sources in `source`, by exactly one of `preset` (a configure preset of the presets file in `source`: `cmake --preset <preset> -B <tree>`, run in `source`) or `generator` (`cmake -S <source> -B <tree> -G <generator>`). `entries` `{name: value}` are cache entries, each passed as `-D<name>=<value>`; a name that is empty or holds `=` is a `ValueError`, and so is a value with whitespace at either end, which CMake would drop from it. Nothing is returned. |
| `cache(tree)` | `cmake cache` | `{name: value}` of every entry of the tree's cache, as CMake recorded it, internal entries included, each value the text CMake holds. Starts no program. |

The cache is read through CMake's file API, never by parsing
`CMakeCache.txt`. Before each configure, `configure` writes a query for the
cache, `tree/.cmake/api/v1/query/client-qiven-devkit/cache-v2`, an empty
file. CMake answers the query each time it generates the tree, its own
reruns during a build included, in `tree/.cmake/api/v1/reply/`. `cache`
reads the reply index whose name is greatest (the current one, by the file
API's rule), the client's `cache-v2` reply it names, and checks its form: kind
`cache`, major version 2, each entry a name and a string value, no name
twice. The reply is CMake's documented machine form. It keeps a value as it
is, trailing spaces included, which the text of `CMakeCache.txt` quotes.

What `cache` gives is what the tree's last generate recorded. A configure
that failed before it generated leaves the earlier reply in place.

Errors: the call base's own, under the call names above. A configure that
fails (a CMake error, a preset the presets file does not hold) is `Failed`,
with CMake's last lines. A tree with no reply index, or one whose index
names no reply to this client's query, is `cmake.NotConfigured` (answer
`not configured`): the tree was never generated with the query in place,
which is the case for a tree configured by any other means. A reply that is
not JSON, is not in the form above, or answers the query with an error is
`Unreadable`.

Not here: building a tree, which is Ninja's (below), and installing it,
which is for the part that installs.

`cmake.FakeCMake(answers)`: the call names above, with the methods'
arguments by keyword (`source`, `tree`, `preset`, `generator`, `entries`).

## Ninja: `ninja`

```
ninja.Ninja(program, env, limit)
  .build(tree, targets=()) -> Built
```

One `Ninja` is Ninja's wrapper. `build` (call name `ninja build`) runs the
program in `tree`, the directory that holds the tree's `build.ninja` (an
absolute path, else a `ValueError`), and
builds `targets` (Ninja's default where none are given). `program` is the
Ninja the tree was generated for; a CMake tree records it as
`CMAKE_MAKE_PROGRAM` in its cache. A tree of the generator `Ninja
Multi-Config` is not here.

**The route.** A build goes through Ninja directly, not through
`cmake --build`. Ninja is the program that runs the steps and prints what it
ran; `cmake --build` starts it and passes its output through. Building
through CMake would put the reading of Ninja's report into CMake's wrapper,
or one call across two wrappers. Built directly, CMake's wrapper configures
and reads the cache, Ninja's builds and reads its report, and each tool has
one wrapper. Nothing is lost: when its build files are out of date, Ninja
reruns CMake itself, by the rule CMake wrote into them.

**The report.** Ninja prints one status line for each step that runs. The
call sets `NINJA_STATUS` in the environment it hands Ninja to its own form,
`@ninja-step %f/%t@ `, replacing any value the caller's `env` holds, so a
step's line is told from what a step's command printed. A status line
names the step by its description in the build file, or by its command
where it has none. Ninja does not end a command's output with a line end
of its own, so a status line can follow such output on the same line: the
form is found anywhere in a line, and what stands before it is the earlier
command's output.

`Built(steps, nothing, said)`:

- `steps`, the name of each step that ran, in the order Ninja printed them.
  Ninja first brings its own build files up to date, and the steps of that
  are listed too (in a CMake tree, the re-check of `CONFIGURE_DEPENDS`
  globs, run by every build, and a rerun of CMake).
- `nothing`, true where Ninja said `ninja: no work to do.`: none of the
  targets asked needed a step. Ninja says it after it has brought its own
  build files up to date, so `nothing` can be true while `steps` names those
  steps. That is the build tool's own word that nothing was rebuilt.
- `said`, every other line Ninja wrote, of its output then of its standard
  error: what the steps' commands printed, and Ninja's own messages.

A step's name must be UTF-8, else the answer is `Unreadable`. The other
lines are decoded as UTF-8 with errors replaced: they are for a reader.
A build that ends with status 0 and holds neither a step nor Ninja's
`no work to do.` is `Unreadable`, as is one that ran a step after it said
there was no work.

Errors: the call base's own, under `ninja build`. A build in which a step
failed is `ninja.BuildFailed` (answer `build failed`), which holds the
`Built` of what ran as `built`, and `failed`, the text after `FAILED: ` of
each failed step's line (its exit code and its outputs, in Ninja's words).
Any other status that is not 0, such as a target Ninja does not know or a
directory with no `build.ninja`, is `Failed`, with Ninja's last lines.

Not here: which steps rebuilt what a later part selects by, and whether a
step a generator adds to every build counts as a rebuild. wrap reports;
the caller reads.

`ninja.FakeNinja(answers)`: the call name `ninja build`, with `tree` and
`targets` by keyword.

## CTest: `ctest`

```
ctest.CTest(program, env, limit)
```

One `CTest` is CTest's wrapper: it lists a build tree's tests and runs a
selection of them. Its `program`, `env` and `limit` hold for each of its
calls. Every call runs CTest in `tree`, an absolute path (else a
`ValueError`), with `--test-dir <tree>` and `--no-tests=error`. A tree that holds no `CTestTestfile.cmake` is
`ctest.NoTestFile` (answer `no test file`), and CTest is not started: CTest
reads such a tree as one with no tests, or ends with status 1 and no word.

| Method | Call name | What it gives |
|---|---|---|
| `listing(tree)` | `ctest listing` | `{name: Test}` of every test of the tree, from `--show-only=json-v1`. A tree with no test gives `{}`: CTest answers its listing with status 0 whatever `--no-tests` says. |
| `run(tree, names, *, junit, jobs, repeat=None, log=None)` | `ctest run` | `Ran` of the tests `names`, run `jobs` at once, their results written by CTest to the file `junit` in JUnit's form and read back. `repeat`, where given, runs each until it fails, at most that many times; `log`, where given, is the file CTest writes its own output to. `junit` and `log` are absolute paths. |

`Test(command, properties)`: `command` is the test's command line, a list of
strings, or None where CTest gives none (a test not available in the tree's
configuration). `properties` is `{name: value}`, each value as CTest's JSON
holds it (a list for `LABELS`, a number for `TIMEOUT`). The listing is
decoded whole and checked: kind `ctestInfo`, major version 1, each test a
name, each property a name and a value, no test's or property's name twice.
Any other answer, or one that is not UTF-8 JSON, is `Unreadable`. Other
fields of a test (its backtrace, its configuration) are not read.

The run takes the names by a file, `--tests-from-file`, written next to
`junit` as `<junit>.names` (one name a line, UTF-8), and kept: the run's
record of what was asked. A name that is empty or holds a line break is a
`ValueError`; a name asked twice is asked once. A file left at `junit` by an
earlier run is removed before CTest starts, so it is never read as this
run's. CTest writes its own working files under `tree/Testing/`.

`Ran(status, results, absent)`:

- `status`, CTest's: 0 when every test it ran passed, 8 when one did not.
- `results`, `{name: Result}` of each test the JUnit holds.
  `Result(status, message, time)`: `status` is the testcase's status in
  CTest's own words, `message` the message of its `failure` or `skipped`
  element (None where it has neither), `time` its seconds. The words, as
  CTest 4.4 writes them: `run`, the test passed; `fail`, it failed
  (message `Failed`, or `Timeout` past its time limit); `notrun`, it did not
  run, either skipped by its own word (message `SKIP_RETURN_CODE=<n>`) or
  not started (message `Unable to find executable`), which CTest counts as a
  failure; `disabled`, the test is disabled. wrap reads none of them as a
  pass or a failure: that is the caller's.
- `absent`, each name asked that the JUnit does not hold, in the order
  asked. CTest drops a name that is no test of the tree without a word, and
  runs the rest, so such a name is never a result, and never a pass.

The JUnit is decoded whole and checked: a `testsuite` root, each testcase a
name that was asked and a status of the four above, no name twice, a time
that is a number. Under `repeat`, it holds each test once, its last run.
Any other file, or none, is `Unreadable`. When the JUnit holds no test,
none of `names` is a test of the tree: `ctest.NoTests` (answer `no tests`).
A status other than 0 and 8 is `Failed`.

`ctest.FakeCTest(answers)`: the call names above, with the methods'
arguments by keyword.

## cdb: `cdb`

```
cdb.Pin(version, sha256)
cdb.Cdb(env, pin)
```

One `Cdb` is the wrapper of cdb, the console debugger of Windows: it finds
cdb on its pin, reads every thread's stack of a running process, and holds a
session on a program it starts or on a dump. cdb is the file that the
variable `QIVEN_CDB` of `env` names, and `env` is the environment cdb, and
every program it starts, runs under. `pin` is the caller's: the version that
cdb's `-version` gives and the SHA-256 of the file, in hex. wrap holds no
pin, looks in no default place and searches no SDK.

| Method | Call name | What it gives |
|---|---|---|
| `find(limit)` | `cdb find` | The path of cdb, once the file is on its pin. |
| `stacks(pid, *, symbols, frames, limit, dump=None)` | `cdb stacks` | `Stacks` of the running process `pid`, and its dump written to `dump`. |
| `session(program, args, *, symbols, cwd, limit)` | `cdb session` | A `Session` on `program`, started under cdb with `args`, at its first stop. |
| `dump(file, *, symbols, limit)` | `cdb dump` | A `Session` on the dump `file`. |

`stacks`, `session` and `dump` find cdb first, within their own limit; a
`Cdb` finds it once, and keeps the path it found for its later calls.

**Finding.** `QIVEN_CDB` unset or empty in `env` is `Missing`, and so is a
value that is not an absolute path or names no file; the reason names the
variable and the pin. A file whose SHA-256 is not the pin's is
`cdb.OffPin` (answer `off its pin`), naming the variable, the file and both
digests. Then `cdb -version` runs: a version other than the pin's is
`OffPin`, naming both versions; an answer with no version is `Unreadable`;
past its limit it is `Slow`, never `Missing`. None of these is a skip: no
answer of `find` reads as "no cdb, nothing to do".

**Each start** of cdb passes `-sins`, so that no symbol path or server is
read from the environment, `-lines`, for source lines, and `-y` with
`symbols`, the directories where the binaries' symbols are, joined by `;`
(no `-y` where there are none). A directory holding `;` is a `ValueError`.

**Stacks.** `cdb -pv -p <pid> -c "~*kn <frames>; .echo @cdb-stacks-end@;
.dump /mt <dump>; qd"`, standard input closed. cdb attaches without
stopping the process: its threads are suspended while cdb reads them, and
resumed when it detaches (`qd`). The mark tells the stacks from what cdb
writes after them. A file left at `dump` is removed before cdb starts, so
it is never read as this run's. `frames` is a number above 0. A `dump`
that is not an absolute path, or holds whitespace, `;` or `"`, is a
`ValueError`: cdb would read it as more than one word.

`Stacks(threads, dump, said)`:

- `threads`, each thread's `Thread(header, frames)`, in cdb's order:
  `header` is the thread's line as cdb wrote it (`.  0  Id: 4d20.13d8
  Suspend: 1 ...`), `frames` each frame's line, the columns' heads dropped.
- `dump`, the path where a file is there after the run, else None.
- `said`, cdb's lines after the stacks, the dump's words among them.

No mark in the answer means the commands never ran: cdb could not attach,
`cdb.NotAttached` (answer `not attached`), with cdb's last lines, such as
`Unable to examine process id ..., Win32 error 0n87`. A mark with no thread
before it is `Unreadable`. Past its limit, the run is `Slow`: what cdb wrote
is not kept, and a process cdb was reading when it was ended may be left
with its threads suspended.

**A session** starts `cdb <program> <args>` held, by `call.start`, its
`limit` covering the whole session. `program` is an absolute path, else a
`ValueError`, and `args` are passed after it as they are. The session reads
cdb's banner to the program's first stop, then turns source lines on
(`.lines -e`) and steps by source line (`l+t`). A dump's session starts
`cdb -z <file>` and reads its banner.

Each method of a session sends cdb one command line and reads its answer:
what cdb writes up to its next prompt (`<n>:<n>> ` at the end of the
output, then 0.05 s with nothing more). The answer is a list of lines,
decoded as UTF-8 with errors replaced, the prompt, empty lines and cdb's
noise dropped: module loads, an event's first line (`.lastevent` gives the
event), `Breakpoint <n> hit`, checksum warnings, NatVis lines and `quit:`.
The program writes to the same pipe, so its own output is among the lines.
Text handed to a method that holds a line break or `;` is a `ValueError`:
cdb would read it as two commands.

| Method | Call name | What it sends; what it gives |
|---|---|---|
| `break_at(spec, binary)` | `cdb break` | `bp`: a breakpoint on a function or a `file:line` in the module of `binary`, unless `spec` names a module (`module!name`). cdb names a module by its file's stem, each character other than a letter, a digit or `_` as `_` (`cdb.module(binary)`). A `file:line` is quoted, so that cdb reads it as a source line. Its lines. |
| `watch(expression)` | `cdb watch` | `ba w8`: a watch of writes to the 8 bytes of the object the C++ expression names. Its lines. |
| `resume()` | `cdb resume` | Runs the program on to its next stop: a `Stop`. |
| `locals()` | `cdb locals` | `dv /t /v`: the frame's locals, each with its type and address. |
| `stack(frames)` | `cdb stack` | `kn <frames>`: the current thread's stack. |
| `threads(frames)` | `cdb threads` | `~*kn <frames>`: every thread's stack. |
| `evaluate(expression)` | `cdb evaluate` | `?? <expression>`: its value, as C++. |
| `step(count)` | `cdb step` | `t <count>`: steps source lines, into calls. |
| `step_over(count)` | `cdb step over` | `p <count>`: steps source lines, over calls. |
| `exception_context()` | `cdb exception context` | `.ecxr`: the frame set to the exception's context. |
| `dump_event()` | `cdb dump event` | A dump's event, as a `Stop` with no lines. |
| `quit()` | `cdb quit` | `q`, which ends the program, then cdb's end, waited up to 10 s. Nothing. |

`Stop(event, lines, exited, fault, own)`: `event` is the last event's text
as `.lastevent` gives it, cdb's chance mark taken off; `lines` the lines
of the resume; `exited` the process's exit status where the event is its
exit, else None; `fault` true for a second-chance exception, the program's
last stop; `own` true for the debugger's own single step, below.

**What `resume` answers.** It sends `g`, then `.lastevent`:

- A first-chance exception other than a single step is the program's: it
  is passed to the program's own handlers (`gn`), unseen, and the run goes
  on to the next stop.
- A first-chance single step (code `80000004`) is the debugger's own. A
  thread's create event falling on the resume from a stop makes the debug
  engine lose that stop, and the lost stop comes as a raw single step. It
  is returned as a `Stop` with `own` true, and the next `resume` answers it
  `gh`, handled, never passed to the program: passed on, the debugger's own
  trap would be the program's fault.

In a dump's `Stop`, `fault` is true for an event with an exception's code
that is not an exit: a dump names no chance.

A session closes by `close()`, or on leaving a `with` block, which ends
cdb's tree, the program in it. Errors: the call base's own, each under its
method's call name. cdb's output ending in the middle of a session is
`Failed`, with cdb's status and its last lines. A `.lastevent` that names
no event is `Unreadable`. Past the session's limit, any method is `Slow`,
and cdb's tree is ended.

Not here: a pin's file or its form, which is the caller's; which cdb a
machine uses; what a caller prints of the stacks, and how much.

`cdb.FakeCdb(answers)`: the call names of `Cdb`'s methods, with their
arguments by keyword. `cdb.FakeSession(answers)`: the call names of a
session's methods, with their arguments by keyword; its `close` does
nothing.
