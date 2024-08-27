import os

from functools import wraps

ENABLE_TRACING_RPD=bool(int(os.getenv("ENABLE_TRACING_RPD", 0)))

class rpd_trace():
    def __init__(self, name="", args=None, skip=False):
        self.skip = skip
        if ENABLE_TRACING_RPD and not self.skip:
            from rpdTracerControl import rpdTracerControl
            rpdTracerControl.skipCreate()
            self.rpd = rpdTracerControl()
            self.name = name
            self.args = args if args else ""

    def _recreate_cm(self):
        return self

    def __call__(self, func):
        if ENABLE_TRACING_RPD and not self.skip:
            if self.name:
                self.name = f"ROCMFA:{self.name}"
                self.name += f":{func.__name__}"
            else:
                self.name = f"ROCMFA:{func.__qualname__}"
            @wraps(func)
            def inner(*args, **kwds):
                with self._recreate_cm():
                    return func(*args, **kwds)
            return inner
        return func

    def __enter__(self):
        if ENABLE_TRACING_RPD and not self.skip:
            self.rpd.start()
            self.rpd.rangePush("python", f"{self.name}", f"{self.args}")
        return self

    def __exit__(self, *exc):
        if ENABLE_TRACING_RPD and not self.skip:
            self.rpd.rangePop()
            self.rpd.stop()
        return False
