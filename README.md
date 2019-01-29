# jit_lang

```
b = function() {
    print(5)

    a = 5
    a = a + 1

    return a
}

b()
b()
b()

a = b
print(a())
```
