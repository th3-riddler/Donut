# Donut Chess Engine

<br>
<div align="center">

![language](https://img.shields.io/github/languages/top/th3-riddler/donut?style=for-the-badge&logo=cplusplus&color=blue)
![GitHub](https://img.shields.io/badge/github-000000?style=for-the-badge&logo=github)
![OS](https://img.shields.io/badge/linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

<img src="donut_img.jpg" width=300 height=300>

</div>
<br>


## What is Donut and how it works
**Donut** is a Chess Engine developed in **C++** and based on the [UCI](https://en.wikipedia.org/wiki/Universal_Chess_Interface) format.  

It uses the [Negamax Algorithm](https://en.wikipedia.org/wiki/Negamax) along with [Alpha-Beta Pruning](https://en.wikipedia.org/wiki/Alpha%E2%80%93beta_pruning) and many other optimization techniques to `search` the best move in a given position.  
More specifically, Donut implements:
- [Opening Book](https://www.chessprogramming.org/Opening_Book)
- [Iterative Deepening](https://www.chessprogramming.org/Iterative_Deepening)
- [Multithreading (Lazy SMP)](https://www.chessprogramming.org/Lazy_SMP)
- [Quiescence Search](https://www.chessprogramming.org/Quiescence_Search)
- [Killer Heuristic](https://www.chessprogramming.org/Killer_Heuristic)
- History Heuristics (Main, Capture, Countermove, and Follow-up)
- [Principal Variation Search](https://www.chessprogramming.org/Principal_Variation_Search)
- Table-driven [Late Move Reductions](https://www.chessprogramming.org/Late_Move_Reductions)
- Dynamic [Null Move Pruning](https://www.chessprogramming.org/Null_Move_Pruning)
- [Razoring](https://www.chessprogramming.org/Razoring)
- [Aspiration Windows](https://www.chessprogramming.org/Aspiration_Windows)
- Lock-free [Transposition Table](https://www.chessprogramming.org/Transposition_Table)

To `evaluate` the position, Donut uses Stockfish's NNUE in order to increase the performance:
1. Big Net: **nn-b1a57edbea57.nnue**
2. Small Net: **nn-baff1ede1f90.nnue**  
## How to use the engine
The main steps to use the engine are:
1. Compile with the `Makefile`:
```bash
make
```
2. Run the compiled file:
```bash
./donut
```
