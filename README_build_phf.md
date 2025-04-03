# build_phf Tool Documentation

The `build_phf` tool is part of the PTHash library and is used to build a Perfect Hash Function (PHF) from a set of keys and associated values. This tool creates a PHF file that can be used for efficient lookups.

## Building the Tool

To build the `build_phf` tool, follow these steps:

```bash
mkdir -p build
cd build
cmake ..
make build_phf
```

## Using the Tool

The `build_phf` tool takes the following command-line arguments:

```
./build_phf <keys.bin> <values.bin> <output.phf> <alpha> <lambda> [seed]
```

Where:
- `<keys.bin>`: Binary file containing keys (uint64_t format)
- `<values.bin>`: Binary file containing values (uint16_t format)
- `<output.phf>`: Output filename for the generated PHF
- `<alpha>`: Load factor (must be > 0 and <= 1.0)
- `<lambda>`: Bucketing parameter (typically around 1.0)
- `[seed]` (optional): Seed for deterministic PHF generation. If provided, ground truth details will be output to stdout.

## File Formats

### Input Files

- **keys.bin**: Binary file starting with a uint64_t count, followed by that many uint64_t keys.
- **values.bin**: Binary file starting with a uint64_t count, followed by that many uint16_t values.

Example of creating a small test file:
```bash
# Create a file with 2 keys (1, 2)
echo -e "\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00" > keys.bin

# Create a file with 2 values (5, 10)
echo -e "\x02\x00\x00\x00\x00\x00\x00\x00\x05\x00\x0A\x00" > values.bin
```

### Output File

The output is a binary file containing the serialized PHF followed by the reordered values. This can be used for efficient lookup operations.

## Generating Ground Truth Details

If you provide a seed value (the optional 6th argument), the tool will output detailed JSON information about the internal structure of the PHF to stdout. This is useful for debugging and verifying the PHF's correctness.

Example:
```bash
./build_phf keys.bin values.bin output.phf 0.9 1.0 42 > ground_truth.json
```

The JSON output contains information about the PHF parameters, internal data structures, and sample key lookups.

## Example Usage

```bash
# Create test files
mkdir -p tmp
echo -e "\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00" > tmp/keys.bin
echo -e "\x02\x00\x00\x00\x00\x00\x00\x00\x05\x00\x0A\x00" > tmp/values.bin

# Build the PHF
cd build
./build_phf ../tmp/keys.bin ../tmp/values.bin ../tmp/output.phf 0.9 1.0 42
```

This will create a PHF in `tmp/output.phf` that maps the keys 1 and 2 to their corresponding values 5 and 10. 