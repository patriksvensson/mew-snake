# Mew Snake

A small experiment to create something concrete using the Mew programming language.  
Made possible with SDL 3 and Sokol, which are two pretty awesome libraries.

![The Mew Snake title screen, with the snake playing by itself](docs/video.gif)

You will need the .NET 10 SDK installed on your machine.  
It currently only works on macOS (Apple silicon).

## Building

Install SDL 3:

```shell
brew install sdl3
```

Install Mew:

```shell
dotnet tool install -g mew --prerelease
```

Then build:

```shell
mew build
```

## Playing

```shell
mew build play
```
