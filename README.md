# Sync Mac

A multi-platform file synchronization system designed to efficiently synchronize files between multiple computers.

## Components

### Connection Module
Handles establishing and maintaining connections between computers.

### Thread Pool
Manages task distribution including file transfer tasks and hash computation tasks.

### Ring Buffer
A thread-safe ring buffer implementation used as a buffer for the thread pool queue and for send/receive buffers between computers.

## Building the Project

```bash
mkdir build && cd build
cmake ..
make
```

## Running Tests

```bash
cd build
ctest
```

## Project Structure

- `src/` - Source code
- `include/` - Header files
- `tests/` - Unit tests
- `docs/` - Documentation
