# Use a stable Ubuntu 22.04 base image
FROM ubuntu:22.04

# Prevent interactive prompts (like timezone selection) during installation
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install all ns-3 compilation tools and graphing dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    gnuplot \
    g++ \
    git \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# 2. Set the working directory inside the container
WORKDIR /usr/local/ns-3

# 3. Copy your entire local ns-3 repository into the Docker container
COPY . /usr/local/ns-3/

# 4. Clean any old Windows/MSYS2 build artifacts that were copied over
RUN rm -rf build/ cmake-cache/

# 5. Configure and build ns-3 natively inside the Linux container
# Note: We configure it identically to your Windows environment parameters
RUN ./ns3 configure --build-profile=default --enable-examples --enable-tests
RUN ./ns3 build

# 6. By default, drop the user right into the bash shell
CMD ["/bin/bash"]
