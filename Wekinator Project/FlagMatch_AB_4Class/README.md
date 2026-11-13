# FlagMatch_AB_4Class_Thinned5

This is a reduced copy of `FlagMatch_AB_4Class`. The original project was not modified.

## Sampling rule

- The dataset is divided into consecutive blocks of 30 examples.
- For Classes 2–4, positions 1, 8, 15, 22, and 30 are retained from each block.
- All 30 Class 1 examples are retained because the default class is already underrepresented.
- The original ID, timestamp, training round, input values, and class label are preserved.

## Dataset size

| Class | Original | Retained |
| --- | ---: | ---: |
| 1 | 30 | 30 |
| 2 | 5,887 | 971 |
| 3 | 5,877 | 992 |
| 4 | 2,276 | 377 |
| **Total** | **14,070** | **2,370** |

## Retraining

The old model is intentionally not copied. Open this project in Wekinator, configure the KNN classifier with `K = 17`, and train a new model from the reduced dataset.

The retained ARFF file may still exceed GitHub's normal 100 MB per-file limit. Keep the model and dataset outside the normal repository or use Git LFS if they need to be shared through GitHub.
