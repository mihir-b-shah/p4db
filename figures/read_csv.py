
import pandas as pd
import sys

'''
TABLE_SIZE = 10M
YCSB_HOT_SIZE = 50 keys
YCSB_HOT_PROB = 75%
	- since all txns are pure hot/cold, we can see NUM_TXNS*YCSB_HOT_PROB is # of txns on switch.
YCSB_REMOTE_PROB = 20%
WORKLOAD = YCSB
NUM_NODES = 8
NUM_TXNS = 500000
CSV_FILE_* = <see in data/>

virtually no aborts.
almost linear scaling with switch_no_conflict, less otherwise

num_txn_workers,switch_no_conflict,ycsb_read_commits,ycsb_write_commits,total_tps,total_cps,total_commits,total_aborts,avg_duration
'''

df = pd.read_csv(sys.argv[1])
df = df[df['run'] == 0]
df = df[df['node_id'] == 0]
df = df[df['ycsb_write_prob'] == 50]
df = df[df['lm_on_switch'] == False]
df = df[df['cc_scheme'] == 'no_wait']
df = df[df['use_switch'] == True]
df = df[df['switch_no_conflict'] == False] #True is optimal data layout, false is worst.

print(df[['num_txn_workers', 'total_cps', 'avg_duration']])
