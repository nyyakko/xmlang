# XMLang

A programming language based on xml.

This project is still very early stage, so if you try to break it... well, what do you even expect?

# Building

You will need a [C++23 compiler](https://github.com/llvm/llvm-project/releases) and [cmake](https://cmake.org/) installed.

```bash
python configure.py && python build.py
```

# Examples

```xml
<program>
    <call name="println">
        <arg value="Hello, World!"></arg>
    </call>
</program>
```

Check [examples](examples/) for more usage examples.

shout out to [beefviper](https://github.com/beefviper/)!!!
