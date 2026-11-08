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

### Errors

Each error is a `call.CallError`, whose fields are `call` (the call's name),
`answer` (the word below) and `reason`. Printed, it reads
`<call>: <answer>: <reason>`.

| Error | `answer` | When |
|---|---|---|
| `Missing` | missing | No file at the path the caller gave, no file for the bare name on the `PATH` of `env`, or no `PATH` in `env`. |
| `Slow` | slow | The program started and passed its limit. It is present: its whole tree has been ended. `limit` holds the limit. The reason says how many of its processes were still alive 10 s after they were ended, if any. |
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
