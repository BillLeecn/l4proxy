a transparent layer-4 proxy for masking network stack characteristcs

## Build and Install

```
autoreconf --install --force
./configure
make
make install
```

## Usage

1. Run the l4proxy daemon.
    ```
    l4proxyd -dp PORT_NUMBER
    ```

2. Redirect any network traffic you'd like to mask to l4proxyd with iptables.
