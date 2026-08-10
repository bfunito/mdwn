# Hello, world!

## C

```c
#include <stdio.h>

int main(void)
{
    puts("Hello, world!");
    return 0;
}
```

## C++

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, world!\n";
    return 0;
}
```

## Java

```java
public class HelloWorld {
    public static void main(String[] args) {
        System.out.println("Hello, world!");
    }
}
```

## Python

```python
def main():
    print("Hello, world!")


if __name__ == "__main__":
    main()
```

## JavaScript

```javascript
function greet(name) {
    console.log(`Hello, ${name}!`);
}

greet("world");
```

## Go

```go
package main

import "fmt"

func main() {
    fmt.Println("Hello, world!")
}
```

## Rust

```rs
fn main() {
    println!("Hello, world!");
}
```

## Ruby

```ruby
def greet(name)
  puts "Hello, #{name}!"
end

greet("world")
```

## PHP

```php
<?php

function greet(string $name): void
{
    echo "Hello, {$name}!\n";
}

greet("world");
```

## Bash

```bash
#!/usr/bin/env bash

name="world"
printf 'Hello, %s!\n' "$name"
```

## Lua

```lua
local function greet(name)
    print("Hello, " .. name .. "!")
end

greet("world")
```

## Haskell

```haskell
module Main where

main :: IO ()
main = putStrLn "Hello, world!"
```

## SQL

```sql
SELECT 'Hello, world!' AS greeting;
```
