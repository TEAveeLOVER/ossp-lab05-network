import matplotlib.pyplot as plt
import os

os.makedirs("../data", exist_ok=True)

labels = ["Repeats", "Random"]
times  = [47, 52]           # средние
ratio  = [8/800, 1600/800]  # 0.01 и 2.0

plt.figure(figsize=(7, 5))
plt.bar(labels, times, color=["#4CAF50", "#2196F3"])
plt.ylabel("Время, мс")
plt.title("ЛР5 вар.21: время 200 COMPRESS (среднее по 3 запускам)")
plt.tight_layout()
plt.savefig("../data/graph1_time.png", dpi=150)
print("OK graph1")

plt.figure(figsize=(7, 5))
plt.bar(labels, ratio, color=["#4CAF50", "#FF9800"])
plt.ylabel("out / in")
plt.title("ЛР5 вар.21: коэффициент размера после RLE")
plt.axhline(1.0, color="gray", linestyle="--", linewidth=1)
plt.tight_layout()
plt.savefig("../data/graph2_ratio.png", dpi=150)
print("OK graph2")
plt.show()