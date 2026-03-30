# Distributed Runs

This project is easiest to scale by running different parameter sets on different
machines, each with the same Docker image.

## Recommended model

Use one machine per experiment batch, for example:

- machine A: `./run_comparison.sh 1 1`
- machine B: `./run_comparison.sh 5 5`
- machine C: `./run_comparison.sh 10 10`

This is the best fit for the current code because each simulation is independent.

## Important note

This does **not** split a single ns-3 simulation across many machines.
For that, you would need an MPI-based ns-3 setup and scenario support for distributed
simulation. That is a separate design path.

## What is already included

The repository includes:

- [Dockerfile](/C:/ns-3/Dockerfile) with build tools and gnuplot
- [docker-compose.yml](/C:/ns-3/docker-compose.yml) to launch the container consistently

## One-time setup on each machine

1. Install Docker Desktop or Docker Engine.
2. Clone or copy this repository to the machine.
3. Build the image:

```bash
docker compose build
```

## Run the environment

Open a shell in the container:

```bash
docker compose run --rm ns3
```

Inside the container, run:

```bash
./run_comparison.sh 1 1
```

Results are written to `results/` on the host because that folder is mounted into
the container.

## Running a batch on different machines

Assign different inputs to each machine. Example:

- machine A: `./run_comparison.sh 1 1`
- machine B: `./run_comparison.sh 1 9`
- machine C: `./run_comparison.sh 5 5`
- machine D: `./run_comparison.sh 10 10`

Then collect the `results/` folders afterward.

## Optional direct one-liner

Instead of opening a shell first, you can run a job directly:

```bash
docker compose run --rm ns3 ./run_comparison.sh 1 1
```

## Sharing with other devices

You can distribute this project in either of these ways:

1. Git repository

```bash
git clone <your-repo-url>
cd ns-3
docker compose build
docker compose run --rm ns3 ./run_comparison.sh 1 1
```

2. Prebuilt Docker image

Build and tag the image on one machine:

```bash
docker build -t ns3-uhr-local:latest .
```

Save it:

```bash
docker save -o ns3-uhr-local.tar ns3-uhr-local:latest
```

Copy `ns3-uhr-local.tar` to another machine, then load it:

```bash
docker load -i ns3-uhr-local.tar
docker compose run --rm ns3 ./run_comparison.sh 1 1
```

## Suggested workflow for research runs

- Keep PCAP disabled unless debugging a protocol issue.
- Keep FlowMonitor enabled for CDF/CSV research outputs.
- Use short simulation times for debugging.
- Use longer simulation times only for final measurements.

