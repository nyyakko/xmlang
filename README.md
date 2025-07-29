# XMLang

A (WIP) programming language based on xml.

<img width="25%" alt="image" src="https://github.com/user-attachments/assets/cfc61819-3101-4ce1-879f-f3627f88ddf5" />\

shout out to [beefviper](https://github.com/beefviper/)!!!

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
