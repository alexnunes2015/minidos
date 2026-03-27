# Change: multithread-gpu-driver

## Objective
Alterar o driver gráfico para suportar multithreading.

## Motivation
Atualmente o driver é single-threaded, limitando performance.

## Scope
- Render pipeline
- Command queue
- Synchronization

## Risks
- Race conditions
- Deadlocks
