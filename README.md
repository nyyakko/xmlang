# xmlc

A (WIP) compiler for a programming language based on xml.

<img width="25%" alt="image" src="https://github.com/user-attachments/assets/cfc61819-3101-4ce1-879f-f3627f88ddf5" />\

shout out to [beefviper](https://github.com/beefviper/)!!!

# Building

You will need a [C++23 compiler](https://github.com/llvm/llvm-project/releases) and [cmake](https://cmake.org/) installed.

```bash
python configure.py && python build.py
```

# Running

To run the compiled program you will need [kubo](https://github.com/nyyakko/kubo).

Once you have it installed, then compile and run like this:

```bash
xmlc -f source.xml && kubo -f program.kubo
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
