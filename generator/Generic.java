
/*
Needs to generate w.r.t. remote key frac, for the hot and cold set.
For reasonable results, this should be a spectrum.
e.g. 100% for hot set --> 0% for cold set.
- we want it to decay with the key popularity.
*/

import java.io.*;
import java.util.*;

public class Generic {

	static final int K_ZIPF = 99;
	static final double K_ROOT_RELAX = 0.1;
	static final int N_OPS = 8;
	static final int N_TXNS = 1_000_000;
	static final int N_NODES = 2;
	static final int N_KEYS = 10_000_000;
	static final int FRAC_DIST_TXNS = 20;

	static String getTxnLine(long[] arr) {
		StringBuilder line = new StringBuilder();
		for (int i = 0; i<N_OPS; ++i) {
			line.append(arr[i]);
			line.append(',');
		}
		line.deleteCharAt(line.length()-1);
		return line.toString();
	}

	static void genUniqueKeys(long[] fill, ScrambledZipfianGenerator suppl) {
		for (int i = 0; i<N_OPS; ++i) {
			while (true) {
				fill[i] = suppl.nextValue();
				boolean unique = true;
				for (int j = 0; j<i; ++j) {
					unique &= (fill[i] != fill[j]);
				}
				if (unique) {
					break;
				}
			}
		}
	}

	static long getKey(long keyBase, int node) {
		return (N_KEYS/N_NODES * node) + keyBase;
	}

	public static void main(String[] args) throws IOException {
		ScrambledZipfianGenerator[] generators = new ScrambledZipfianGenerator[N_NODES];
		long[][][] txns = new long[N_NODES][N_TXNS][N_OPS];

		for (int i = 0; i<N_NODES; ++i) {
			HashMap<Long, Integer> keyCts = new HashMap<>();
			generators[i] = new ScrambledZipfianGenerator(0, (N_KEYS-1)/N_NODES, (double) K_ZIPF/100);
			for (int t = 0; t<N_TXNS; ++t) {
				genUniqueKeys(txns[i][t], generators[i]);
			}

			long[][] nodeTxns = txns[i];
			int maxCt = 0;

			for (long[] txn : nodeTxns) {
				for (long op : txn) {
					if (!keyCts.containsKey(op)) {
						keyCts.put(op, 0);
					}
					keyCts.put(op, 1+keyCts.get(op));
					maxCt = Math.max(maxCt, keyCts.get(op));
				}
			}

			String fname = String.format("node%d_z%d_N%d_n%d_k%d_txns.csv",
				i, K_ZIPF, N_TXNS, N_OPS, N_KEYS);
			PrintWriter outFile = new PrintWriter(new File(fname));

			for (long[] txn : nodeTxns) {
				for (int j = 0; j<txn.length; ++j) {
					int rand = (int) (100*Math.random());
					// outFile.printf("op: %d, rand: %d, distFrac: %d\n", txn[j], rand, distFrac);
					if (rand < FRAC_DIST_TXNS) {
						// randomly pick something from one of our nodes.
						txn[j] = getKey(txn[j], (int) (Math.random()*N_NODES));
					} else {
						// its my version
						txn[j] = getKey(txn[j], i);
					}
				}

				outFile.println(getTxnLine(txn));
			}

			outFile.flush();
			outFile.close();
			System.out.printf("Done! %d\n", nodeTxns.length);
		}

		// build the layout, mimicking even_better_layout.
		HashMap<Long, Integer> keyFreqs = new HashMap<>();
		for (int i = 0; i<N_NODES; ++i) {
			for (int j = 0; j<N_TXNS; ++j) {
				for (int k = 0; k<N_OPS; ++k) {
					if (!keyFreqs.containsKey(txns[i][j][k])) {
						keyFreqs.put(txns[i][j][k], 0);
					}
					keyFreqs.put(txns[i][j][k], keyFreqs.get(txns[i][j][k])+1);
				}
			}
		}
		ArrayList<Map.Entry<Long, Integer>> sortedFreqs = new ArrayList<>(keyFreqs.entrySet());
		Collections.sort(sortedFreqs, (Map.Entry<Long, Integer> e1, Map.Entry<Long, Integer> e2) -> {
			return Integer.compare(e2.getValue(), e1.getValue());
		});

		String dist_fname = String.format("dist_z%d_N%d_n%d_k%d.txt",
			K_ZIPF, N_TXNS, N_OPS, N_KEYS);
		PrintWriter dist_pw = new PrintWriter(new File(dist_fname));
		for (Map.Entry<Long, Integer> entry : sortedFreqs) {
			dist_pw.printf("%d:%d\n", entry.getKey(), entry.getValue());
		}
		dist_pw.flush();
		dist_pw.close();
		System.out.printf("Done w/ dist!\n");
	}
}
