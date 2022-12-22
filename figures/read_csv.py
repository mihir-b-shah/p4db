
import pandas as pd
import sys
    
def ycsb(df):
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
    '''
    df = df[df['workload'] == 'ycsb']
    df = df[df['ycsb_write_prob'] == 50]
    return df

def smallbank(df):
    '''
    smallbank_table_size=1M
    smallbank_hot_prob=0.9
    smallbank_remote_prob=0.2
    smallbank_hot_size=5
    smallbank_write_prob=0.85

    smallbank_amalgamate_commits,smallbank_balance_commits,smallbank_deposit_checking_commits,smallbank_send_payment_commits,smallbank_write_check_commits,smallbank_transact_saving_commits
    #print(df[['smallbank_amalgamate_commits', 'smallbank_balance_commits', 'smallbank_deposit_checking_commits', 'smallbank_send_payment_commits', 'smallbank_write_check_commits', 'smallbank_transact_saving_commits']].sum(axis=1), '\n', df[['total_commits', 'total_txns']])

    Interestingly commit ratios go down?
    '''
    df = df[df['workload'] == 'smallbank']
    return df

def tpcc(df):
    df = df[df['workload'] == 'tpcc']
    return df
    
df = pd.read_csv(sys.argv[1])
if sys.argv[2] == 'ycsb':
    df = ycsb(df)
elif sys.argv[2] == 'smallbank':
    df = smallbank(df)
elif sys.argv[2] == 'tpcc':
    df = tpcc(df)
else:
    raise NotImplementedError

df = df[df['run'] == 0]
df = df[df['node_id'] == 0]
df = df[df['lm_on_switch'] == False]
df = df[df['cc_scheme'] == 'no_wait']
df = df[df['use_switch'] == True]
df = df[df['switch_no_conflict'] == bool(int(sys.argv[3]))] #True is optimal data layout, false is worst.
print(df[['num_txn_workers', 'total_cps', 'total_tps', 'txn_latency']])
