Assuming Simphony is properly installed on the system, compile and run this
example by simply doing from this directory:

```bash
cmake -S . -B build
cmake --build build
./simphox
```

It generates a configurable set of optical photons using the built-in torch
configuration, converts them into an NP array, prints the data, and saves it as
`out/photons.npy`.
