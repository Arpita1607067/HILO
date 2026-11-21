# In-row mapping: latency, energy and area of y = A·x

This folder evaluates a 32-bit matrix-vector multiplication y = A·x on 512 × 512 MAGIC crossbars. Several dot-product terms share one crossbar row. Five techniques are compared: ULTRA, SIMPLER, LOGIC, AUTO and HILO. All results are normalized to ULTRA.

| File | Content |
|---|---|
| `hilo_shared_row_mapping.ipynb` | The whole evaluation. Run the cells from top to bottom. |
| `lookuptable_with_functional_cells.csv` | Adder library: the columns of `../lookuptable_with_weights.csv` plus `abc_gates` and `functional_cells` (the functional cells each adder needs). |

---

## Run

Requirements: Python 3 with `numpy`, `scipy`, `pandas`, `matplotlib`, `jinja2` and Jupyter.

```bash
pip install numpy scipy pandas matplotlib jinja2 jupyter
cd row_mapping
jupyter notebook hilo_shared_row_mapping.ipynb      # then: Run → Run All Cells
```

Run without opening the notebook:

```bash
cd row_mapping
jupyter nbconvert --to notebook --execute hilo_shared_row_mapping.ipynb --output executed.ipynb
```

**Google Colab.** Open the notebook in Colab and run all cells. Section 2 asks you to upload `lookuptable_with_functional_cells.csv` if it is not in the Colab files yet.

A full run takes about 2 minutes. Most of that time downloads the 15 benchmark matrices into `matrices/`. The notebook also writes its four modules into the folder (`hilo_cost.py`, `hilo_prep.py`, `own_adder.py`, `dense_rowmap.py`).

### What the notebook does

| Section | Content |
|---|---|
| 1. Parameters | Crossbar size, precision, SET mode, link model, cycle time. The defaults give the results below. |
| 2. Lookup table | Loads `lookuptable_with_functional_cells.csv`. |
| 3. Modules | `hilo_cost.py`: cost of one 32-bit HILO / AUTO multiplication from its adder list. `hilo_prep.py`: PREP parallel SET. `own_adder.py`: each technique's own adder for the reduction. `dense_rowmap.py`: row layout, dense row mapping, tree reduction, latency / energy / area. |
| 4. Every technique | Cells, terms per row and cycles of one multiplication for each technique, and the HILO row stage by stage. |
| 5. eris1176 | y = A·x on one matrix, with every phase. |
| 6. 15 benchmarks | Downloads the matrices, runs every technique, normalizes to ULTRA and reports HILO's improvement. |

Main parameters (section 1):

| Parameter | Default | Meaning |
|---|---|---|
| `XBAR_ROWS`, `ROW_CELLS` | 512, 512 | Crossbar size |
| `N` | 32 | Precision (bits) |
| `SET_MODE` | `"prep"` | `"prep"` = PREP parallel SET, `"greedy"` = SET per adder step |
| `MUL_SOURCE` | `"paper"` | `"paper"` = adder gates of the 32-bit HILO (8,081) and AUTO (8,462) multiplications; `"computed"` = from the adder list and lookup table |
| `LINKS` | `"hier"` | Crossbar-to-crossbar copies: `"hier"` (hierarchical bus), `"shared"` (one bus), `"parallel"` (a link per crossbar pair) |
| `BUS_FANOUT` | 16 | Crossbars per level-1 bus for `"hier"` |
| `X_ORDER` | `"sequential"` | Vector values arrive one after another from the RAM buffer |
| `DROP_ZERO_COLUMNS` | `True` | All-zero matrix columns are dropped |
| `T_CYCLE_NS` | 1.0 | ns per cycle |

---

## Row layout

Matrix row *i* is mapped to crossbar row *i* (mod 512) and stored densely: all values, zeros included. One crossbar row holds *k* matrix values, one shared x slot and one set of working cells, which every multiplication of that row reuses. The 64-bit products accumulate at the end of the row. The partial sums of a matrix row are then added across its crossbars with a tree (⌈log₂ C⌉ levels of copy + add), with each technique's own adder.

HILO row (512 cells, k = 4):

| Stage | Matrix | x slot | Working cells | Products | Unused |
|---|---:|---:|---:|---:|---:|
| 1: m1·a | 128 | 32 | 81 | 64 | 207 |
| 2: m2·b | 128 | 32 | 81 | 128 | 143 |
| 3: m3·c | 128 | 32 | 81 | 192 | 79 |
| 4: m4·d | 128 | 32 | 81 | 256 | 15 |
| Final sum | 128 | – | – | 256 | 0 |

---

## Results

Default parameters (PREP SET, hierarchical links, 512 × 512 crossbars, 32-bit).

### One 32-bit multiplication per technique

| Technique | Cells per mult | Working cells | Rows per band | Terms per band (k) | Cycles per mult | Row-boundary copies | 64-bit add (cycles) |
|---|---:|---:|---:|---:|---:|---:|---:|
| ULTRA | 15,071 | 14,943 | 30 | 4 | 15,007 | 3,712 | 769 |
| SIMPLER | 635 | 507 | 2 | 5 | 12,870 | 128 | 769 |
| LOGIC | 235 | 107 | 1 | 3 | 11,317 | 0 | 734 |
| AUTO | 220 | 92 | 1 | 4 | 9,119 | 0 | 762 |
| HILO | 209 | 81 | 1 | 4 | 8,688 | 0 | 619 |

HILO's 8,688 cycles = 8,081 adder gates + 410 carry-copy NOTs + 197 SETs. AUTO's 9,119 = 8,462 + 452 + 205.

### 15 benchmarks, normalized to ULTRA

| Matrix | Latency SIMPLER | Latency LOGIC | Latency AUTO | Latency HILO | Energy SIMPLER | Energy LOGIC | Energy AUTO | Energy HILO | Area SIMPLER | Area LOGIC | Area AUTO | Area HILO |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| eris1176 | 0.6947 | 0.6051 | 0.4879 | 0.4648 | 0.0507 | 0.0269 | 0.0220 | 0.0208 | 0.0573 | 0.0571 | 0.0429 | 0.0429 |
| cegb2919 | 0.6947 | 0.6050 | 0.4877 | 0.4646 | 0.0495 | 0.0219 | 0.0179 | 0.0169 | 0.0558 | 0.0465 | 0.0349 | 0.0349 |
| raefsky1 | 0.6946 | 0.6050 | 0.4876 | 0.4646 | 0.0482 | 0.0230 | 0.0188 | 0.0177 | 0.0545 | 0.0489 | 0.0366 | 0.0366 |
| fxm3_6 | 0.6946 | 0.6049 | 0.4875 | 0.4645 | 0.0479 | 0.0212 | 0.0174 | 0.0164 | 0.0541 | 0.0450 | 0.0338 | 0.0338 |
| Na5 | 0.6946 | 0.6048 | 0.4875 | 0.4645 | 0.0474 | 0.0219 | 0.0179 | 0.0169 | 0.0535 | 0.0465 | 0.0349 | 0.0349 |
| EX5 | 0.6946 | 0.6049 | 0.4875 | 0.4645 | 0.0479 | 0.0212 | 0.0173 | 0.0163 | 0.0540 | 0.0450 | 0.0338 | 0.0338 |
| fp | 0.6945 | 0.6049 | 0.4875 | 0.4644 | 0.0479 | 0.0212 | 0.0174 | 0.0164 | 0.0541 | 0.0450 | 0.0338 | 0.0338 |
| ex40 | 0.6946 | 0.6048 | 0.4875 | 0.4644 | 0.0482 | 0.0220 | 0.0180 | 0.0170 | 0.0544 | 0.0468 | 0.0351 | 0.0351 |
| benzene | 0.6945 | 0.6048 | 0.4875 | 0.4644 | 0.0483 | 0.0220 | 0.0180 | 0.0170 | 0.0545 | 0.0468 | 0.0351 | 0.0351 |
| bcsstk33 | 0.6945 | 0.6048 | 0.4875 | 0.4644 | 0.0483 | 0.0220 | 0.0180 | 0.0169 | 0.0545 | 0.0467 | 0.0350 | 0.0350 |
| graham1 | 0.6946 | 0.6048 | 0.4875 | 0.4644 | 0.0480 | 0.0212 | 0.0174 | 0.0164 | 0.0541 | 0.0451 | 0.0338 | 0.0338 |
| net25 | 0.6945 | 0.6048 | 0.4874 | 0.4644 | 0.0481 | 0.0213 | 0.0174 | 0.0164 | 0.0543 | 0.0452 | 0.0339 | 0.0339 |
| bundle1 | 0.6945 | 0.6048 | 0.4874 | 0.4644 | 0.0478 | 0.0211 | 0.0173 | 0.0163 | 0.0539 | 0.0449 | 0.0337 | 0.0337 |
| Si10H16 | 0.6945 | 0.6048 | 0.4874 | 0.4644 | 0.0473 | 0.0212 | 0.0174 | 0.0164 | 0.0533 | 0.0451 | 0.0338 | 0.0338 |
| Goodwin_040* | 0.6945 | 0.6048 | 0.4874 | 0.4644 | 0.0477 | 0.0214 | 0.0175 | 0.0165 | 0.0538 | 0.0455 | 0.0341 | 0.0341 |
| **GMean** | **0.6946** | **0.6049** | **0.4875** | **0.4645** | **0.0482** | **0.0219** | **0.0180** | **0.0169** | **0.0544** | **0.0466** | **0.0350** | **0.0350** |

ULTRA = 1.0000 in every column. \* Goodwin_040 could not be downloaded from the mirror, so it is assumed to have no all-zero columns.

### HILO improvement over the 15 benchmarks

Positive % = HILO is lower. (×) = the other technique's value divided by HILO's.

| Metric | vs ULTRA | vs SIMPLER | vs LOGIC | vs AUTO |
|---|---:|---:|---:|---:|
| Latency (geometric mean) | 53.55% (2.15×) | 33.13% (1.50×) | 23.21% (1.30×) | 4.73% (1.05×) |
| Energy (geometric mean) | 98.31% (59.11×) | 64.90% (2.85×) | 22.83% (1.30×) | 5.79% (1.06×) |
| Area (geometric mean) | 96.50% (28.61×) | 35.75% (1.56×) | 24.99% (1.33×) | 0.00% (1.00×) |

Totals over the 15 MVMs:

| Metric | ULTRA | SIMPLER | LOGIC | AUTO | HILO |
|---|---:|---:|---:|---:|---:|
| Latency (µs) | 2,266,961 | 1,574,498 | 1,371,138 | 1,105,054 | 1,052,844 |
| Energy (µJ) | 2,724,416 | 130,151 | 58,382 | 47,818 | 45,047 |
| Area (M cells) | 4,939,338 | 266,318 | 224,963 | 168,734 | 168,734 |

### eris1176 in detail

| Technique | Crossbars | Tree levels | Latency (µs) | Energy (µJ) | Area (M cells) | Area utilization (%) |
|---|---:|---:|---:|---:|---:|---:|
| ULTRA | 20,580 | 9 | 22,031.217 | 2,974.634 | 5,394.92 | 98.43 |
| SIMPLER | 1,180 | 8 | 15,304.006 | 150.688 | 309.33 | 91.28 |
| LOGIC | 1,176 | 9 | 13,331.861 | 79.899 | 308.28 | 63.85 |
| AUTO | 882 | 9 | 10,749.218 | 65.522 | 231.21 | 75.96 |
| HILO | 882 | 9 | 10,240.430 | 61.738 | 231.21 | 74.32 |
