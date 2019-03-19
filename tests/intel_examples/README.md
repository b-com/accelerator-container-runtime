# Accelerator-container tests with Intel OPAE examples  

Directory content:

* `Dockerfile`: build the intel_fpga image, a CentOS 7 image containing OPAE libraries and tools.

* `Dockerfile.nlb0`: build an image running hello_fpga test program.

* `Dockerfile.nlb3`: build an image running fpga_diag (ie nlb3) tool to measure read/write bandwitdh.


# Populate folder

- Copy intel_examples directory to the server

`scp -P <port> -r intel_examples BCOM_FPGAnode:/tmp`

- Connect to the server

- Copy OPAE rpm package(s) to intel_examples directory

```
cd /tmp/intel_examples
cp <path>/dcp_1_0_beta/sw/opae-*tools.rpm .
```

- Copy hello_fpga test program from opae build directory

`cp <path>/opae-0.13.0-1/build/bin/hello_fpga .`


# Build docker images

```
docker build --rm -t intel_fpga .
docker build --rm -f Dockerfile.nlb0 -t hello_fpga .
docker build --rm -f Dockerfile.nlb3 -t fpga_diag .
```

# Run appplications

```
docker run --runtime=accelerator --rm hello_fpga
docker run --runtime=accelerator --rm fpga_diag
```
(based on runtime-tool which is able to load expected accelerator function)
 
