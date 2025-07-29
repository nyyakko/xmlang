# XMLang

a programming language based on xml.

this project is still very early stage, so if you try to break it... well, what do you even expect?

# Building

You will need a [C++23 compiler](https://github.com/llvm/llvm-project/releases) and [cmake](https://cmake.org/) installed.

```bash
python configure.py && python build.py
```

# Examples

## Hello, World!

```xml
<program>
    <call name="println">
        <arg value="Hello, World!"></arg>
    </call>
</program>
```

## Hello, World! (fancy)

```xml
<program>
    <function name="main" result="none">
        <call name="println">
            <arg>Hello, World!</arg>
        </call>
    </function>
</program>
```

shout out to [beefviper](https://github.com/beefviper/)!!!
