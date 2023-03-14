
import java.util.*;
import java.io.*;

public class HashAnalyzer {
	static final int N_OPS = 8;
	static final int N_NODES = 2;
	//	distributed fraction is 20%, and 2 nodes: so 20*(2-1)/2 = 10% additionally local.
	//	TODO maybe I need multiple remote periods
	static final int FRAC_REMOTE = 10;
	static final int N_KEYS = 10_000_000;
	static final int N_SCHEDULES = 10;
	static final int SCHED_LEN = 10;
	static final int FREQ_ACCEL_THR = 720;
	static final int MINI_BATCH_N_CONSIDER = 5000;
	static final int MAX_ALLOW_ABORT_CT = 1;

	static final boolean DO_SCHED = true;

	static class Txn {
		int abortCt;
		List<Integer> ops;

		Txn(List<Integer> ops) {
			this.abortCt = 0;
			this.ops = ops;
		}
	};

	static int nodeForKey(int k) {
		return k / (N_KEYS/N_NODES);
	}

	public static void main(String[] args) throws IOException {
		assert(args.length == N_NODES+1);

		String distName = args[0];
		Scanner dsc = new Scanner(new File(distName));
		dsc.useDelimiter("\n|\\s|:");
		Map<Integer, Integer> freqMap = new HashMap<>();
		while (dsc.hasNextInt()) {
			freqMap.put(dsc.nextInt(), dsc.nextInt());
		}
		assert(!dsc.hasNextInt());
		dsc.close();

		List<List<List<Integer>>> schedules = new ArrayList<>();
		for (int n = 0; n<N_NODES; ++n) {
			schedules.add(new ArrayList<>());
			for (int s = 0; s<N_SCHEDULES; ++s) {
				schedules.get(n).add(new ArrayList<>());
				for (int p = 0; p<SCHED_LEN; ++p) {
					schedules.get(n).get(s).add(s == p ? (n == 0 ? 1 : 0) : (n == 0 ? 0 : 1));
				}
			}
		}
		
		int allHotCt = 0;
		int discardTxnCt = 0;

		List<Map<Integer, Queue<Txn>>> scheduledTxns = new ArrayList<>();
		for (int i = 0; i<N_NODES; ++i) {
			Map<Integer, Queue<Txn>> m = new HashMap<>();
			for (int p = 0; p<SCHED_LEN; ++p) {
				m.put(p, new LinkedList<>());
			}
			scheduledTxns.add(m);
		}

		for (int f = 1; f<args.length; ++f) {
			int node = f-1;
			String traceName = args[f];
			Scanner sc = new Scanner(new File(traceName));
			sc.useDelimiter("\\s|\n|,");
			// java makes me sad
			Queue<Txn> txns = new LinkedList<>();
			while (sc.hasNextInt()) {
				List<Integer> ops = new ArrayList<>();
				for (int i = 0; i<N_OPS; ++i) {
					int k = sc.nextInt();
					if (freqMap.get(k) < FREQ_ACCEL_THR) {
						ops.add(k);
					}
				}
				Collections.sort(ops, (Integer i1, Integer i2) -> {
					return Integer.compare(freqMap.get(i2), freqMap.get(i1));
				});
				if (ops.size() != 0) {
					if (DO_SCHED) {
						List<Integer> sched0 = schedules.get(nodeForKey(ops.get(0))).get(ops.get(0).hashCode() % N_SCHEDULES);
						List<Integer> sched1 = ops.size() >= 2 ? schedules.get(nodeForKey(ops.get(1))).get(ops.get(1).hashCode() % N_SCHEDULES) : null;

						// put in the lightest queue that satisfies both constraints.
						int sBest = -1;
						if (sched1 != null) {
							for (int s = 0; s<SCHED_LEN; ++s) {
								if (sched0.get(s) == node && sched1.get(s) == node && (sBest == -1 ||
									scheduledTxns.get(node).get(sBest).size() > scheduledTxns.get(node).get(s).size())) {
									sBest = s;
								}
							}
						}
						if (sBest == -1) {
							// now only satisfy top key constraint.
							for (int s = 0; s<SCHED_LEN; ++s) {
								if (sched0.get(s) == node && (sBest == -1 ||
									scheduledTxns.get(node).get(sBest).size() > scheduledTxns.get(node).get(s).size())) {
									sBest = s;
								}
							}
						}
						assert(sBest != -1);
						scheduledTxns.get(node).get(sBest).offer(new Txn(ops));
					} else {
						scheduledTxns.get(node).get((int) (SCHED_LEN*Math.random())).offer(new Txn(ops));
					}
				} else {
					allHotCt += 1;
				}
			}
			assert(!sc.hasNextInt());
			sc.close();
		}
		System.out.printf("Removed %d all-hot txns.\n", allHotCt);

		int miniBatchNum = 0;
		int txnNum = 0;
		Set<Integer> nodesDone = new HashSet<>();
		Map<Integer, Integer> locks = new HashMap<>();
		int commitCt = 0;
		int abortCt = 0;
		Map<Integer, Integer> txnAbortCts = new HashMap<>();
		for (int i = 0; i<=MAX_ALLOW_ABORT_CT; ++i) {
			txnAbortCts.put(i, 0);
		}

		timeLoop:
		while (true) {
			for (int n = 0; n<N_NODES; ++n) {
				Map<Integer, Queue<Txn>> m = scheduledTxns.get(n);
				if (m.size() == 0) {
					nodesDone.add(n);
					if (nodesDone.size() == N_NODES) {
						break timeLoop;
					} else {
						continue;
					}
				}
				
				if (!m.containsKey(miniBatchNum % SCHED_LEN)) {
					continue;
				}
				Queue<Txn> nodeTxns = m.get(miniBatchNum % SCHED_LEN);
				if (nodeTxns.isEmpty()) {
					m.remove(miniBatchNum % SCHED_LEN);
					continue;
				}
				Txn txn = nodeTxns.poll();
				// run the txn.
				boolean ok = true;
				for (int k : txn.ops) {
					Integer e = locks.get(k);
					if (e != null && e != n) {
						ok = false;
						break;
					}
				}
				if (ok) {
					for (int k : txn.ops) {
						locks.put(k, n);
					}
					txnAbortCts.put(txn.abortCt, txnAbortCts.get(txn.abortCt)+1);
					commitCt += 1;
				} else {
					abortCt += 1;
					txn.abortCt += 1;
					if (txn.abortCt <= MAX_ALLOW_ABORT_CT) {
						nodeTxns.offer(txn);
					} else {
						discardTxnCt += 1;
					}
				}
			}
			txnNum += 1;
			if (txnNum % MINI_BATCH_N_CONSIDER == 0) {
				for (int n = 0; n<scheduledTxns.size(); ++n) {
					Map<Integer, Queue<Txn>> m = scheduledTxns.get(n);
					System.out.printf("Node %d, discard_ct %d | ", n, discardTxnCt);
					for (Map.Entry<Integer, Queue<Txn>> e : m.entrySet()) {
						System.out.printf("(p=%d, qsize=%d) ", e.getKey(), e.getValue().size());
					}
					System.out.println();
				}
				System.out.println();
				miniBatchNum += 1;
				locks.clear();
			}
		}
		System.out.printf("Timesteps: %d\n", txnNum);
		System.out.printf("Discarded txns ct: %d\n", discardTxnCt);
		System.out.printf("Commit ct: %d\n", commitCt);
		System.out.printf("Abort ct: %d\n", abortCt);
		System.out.printf("Txn abort cts: %s\n", txnAbortCts.toString());
	}
}
