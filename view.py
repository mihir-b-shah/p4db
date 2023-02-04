
from matplotlib import pyplot as plt

xs = []
for line in open('generator/meme', 'r'):
	xs.append(int(line.rstrip('\n')))
plt.hist(xs, bins=1000)
plt.show()
