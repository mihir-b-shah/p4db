
import random
import statistics
from matplotlib import pyplot as plt
from scipy.stats import norm
import numpy as np

TXN_SIZE = 10
HOT_FRAC = 5 #1 in 5
NUM_BATCHES = 1000
HOT_TUPLES = 1000
NUM_TUPLES = 1000000
BATCH_STATIONS = 100

# todo better distribution
def gen_tupl():
	cond = random.randint(0, HOT_FRAC-1)
	if cond == 0:
		return random.randint(0, HOT_TUPLES-1)
	else:
		return random.randint(0, NUM_TUPLES-1)

def gen_txn():
	return [gen_tupl() for i in range(TXN_SIZE)]

def conflicts(tset, mod):
	good = True
	for tupl in mod:
		if tupl in tset:
			good = False
	if good:
		tset.update(mod)
		return False
	else:
		return True
	
def pick_evict(stations):
	idx = 0
	for i in range(len(stations)):
		if len(stations[idx]) < len(stations[i]):
			idx = i
	return idx

def run():
	stations = []
	cts = []
	for i in range(BATCH_STATIONS):
		stations.append(set())
		cts.append(0)
	batch_sizes = []

	for i in range(NUM_BATCHES):
		while True:
			mod = gen_txn()
			found = False
			for j in range(BATCH_STATIONS):
				if not conflicts(stations[j], mod):
					found = True
					cts[j] += 1
					break
			if not found:
				evict_idx = pick_evict(stations)
				batch_sizes.append(cts[evict_idx])
				stations[evict_idx] = set()

				stations[evict_idx].update(mod)
				cts[evict_idx] = 1
				break

	return batch_sizes

batches = run()
u = statistics.mean(batches)
sd = statistics.stdev(batches)
print(u, sd)

