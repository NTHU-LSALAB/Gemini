# Gemini

## About

Gemini is an efficient GPU resource sharing system with fine-grained control for Linux platforms.

It shares a NVIDIA GPU among multiple clients with specified resource constraint, and works seamlessly with any CUDA-based GPU programs. Besides, it is also work-conserving and with low overhead, so nearly no compute resource waste will happen.

## System Structure

Gemini consists of two components: **scheduler** (back-end) and **hook library** (front-end).

- *scheduler* (*GPU device manager*) (`gem-schd`): A daemon process managing token. Based on information provided in resource configuration file (`resource.conf`), scheduler determines whom to give token. Clients can launch CUDA kernels only when holding a valid token.
- *hook library* (`libgemhook.so.1`): A library intercepting CUDA-related function calls. It utilizes the mechanism of `LD_PRELOAD`, which forces our hook library being loaded before any other dynamic linked libraries.

Currently we use **Unix domain socket** as the communication interface between components.

## Installation

### Dependencies

* `libzmq`
* `glib-2.0`
* `gio-2.0`

### Compile

Basically all components can be built with the following command:

```
make [CUDA_PATH=/path/to/cuda/installation] [PREFIX=/place/to/install] [DEBUG=1]
```

This command will install the built binaries in `$(PREFIX)/bin` and `$(PREFIX)/lib`. Default value for `PREFIX` is `$(pwd)/..`.

Adding `DEBUG=1` in above command will make hook library and scheduler output more scheduling details.

## Usage

### Resource configuration file format

We follow the syntax of [XDG Desktop Entry Specification](https://specifications.freedesktop.org/desktop-entry-spec/latest/) for configuration file.

Settings for a client group is described by a section like below:

```ini
[client_group_name]
MinUtil = 0.1  # minimum required ratio of GPU usage time (between 0 and 1); default is 0
MaxUtil = 0.5  # maximum allowed ratio of GPU usage time (between 0 and 1); default is 1
MemoryLimit = 2GiB  # maximum allowed GPU memory usage (in bytes); default is 1GiB
```

Suffixes like `M`, `GB` will be interpreted as power of 10 (e.g. 1 000 000 and 1 000 000 000), and suffixes like `Ki`, `MiB` will be interpreted as power of 2 (e.g. 1 024 and 1 048 576).

Default values will be used for unspecified parameters.

Changes to this file will be monitored by `gem-schd`. After each change, scheduler will read this file again and update settings.

### Run

Specify a directory in environment variable `GEMINI_IPC_DIR` for keeping unix domain socket files. Default value is `/tmp/gemini/ipc`.

When launching applications, set environment variable `GEMINI_GROUP_NAME` to name of client group and `LD_PRELOAD` to location of `libgemhook.so.1`.

For convenience, we provide a Python script `tools/launch-command.py` for launching applications. Refer to scripts and source codes for more details.

## Contributors

[jim90247](https://github.com/jim90247)
[eee4017](https://github.com/eee4017)
[ncy9371](https://github.com/ncy9371)

