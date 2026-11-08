"""The one form of every wrapper's fake: answers by call name, calls recorded,
no program started. See this directory's README.md.
"""


class Unanswered(LookupError):
    """A fake was called by a name its answers do not hold."""


class Fake:
    """A wrapper's fake: `answers` {call name: answer}; `calls`, each call
    made, (name, {argument: value}), in order."""

    def __init__(self, answers):
        self.answers = dict(answers)
        self.calls = []

    def _answer(self, name, **arguments):
        """The answer to the call `name`: raised where it is an exception,
        called with the arguments where it is callable, else as it is."""
        self.calls.append((name, arguments))
        if name not in self.answers:
            raise Unanswered(f"{type(self).__name__}: no answer for {name}")
        answer = self.answers[name]
        if isinstance(answer, BaseException):
            raise answer
        if callable(answer):
            return answer(**arguments)
        return answer
