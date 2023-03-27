
import java.util.*;
import java.io.*;

public class DisjointChecker {
    // just check for two nodes for simplicity.

    static HashMap<Integer, HashSet<Integer>> getBatchKeys(int node) throws FileNotFoundException, IOException {
        Scanner sc = new Scanner(new File(String.format("log_%d", node)));
        HashMap<Integer, HashSet<Integer>> batchKeys = new HashMap<>();
        while (sc.hasNextInt()) {
            int nodeNum = sc.nextInt();
            int mbNum = sc.nextInt();
            int k = sc.nextInt();

            assert(nodeNum == 0);
            if (!batchKeys.containsKey(mbNum)) {
                batchKeys.put(mbNum, new HashSet<>());
            }
            batchKeys.get(mbNum).add(k);
        }
        sc.close();
        return batchKeys;
    }

    public static void main(String[] args) throws FileNotFoundException, IOException {
        HashMap<Integer, HashSet<Integer>> batchKeys0 = getBatchKeys(0);
        HashMap<Integer, HashSet<Integer>> batchKeys1 = getBatchKeys(1);

        for (int mbNum : batchKeys0.keySet()) {
            HashSet<Integer> keys0 = batchKeys0.get(mbNum);
            HashSet<Integer> keys1 = batchKeys1.get(mbNum);

            HashSet<Integer> combine = new HashSet<>(keys0);
            combine.addAll(keys1);
            assert(keys0.size() + keys1.size() == combine.size());
            System.out.printf("Checked mb %d, had |keys0|=%d, |keys1|=%d, |combine|=%d\n", mbNum, keys0.size(), keys1.size(), combine.size());
        }
    }
}
