# Project 3 Design Log

**P2 code used:** My own P2 code, copied from my P2 repository.

**Tools and contributions:** Claude implemented the three P3 components and most of the P3 tests, substantially revising my earlier work. My contributions included integrating my P2 code, reviewing the implementations, adding additional tests, verifying the hash-table measurements, and revising the design explanations.

## 1. Hash table

The registry uses separate chaining: each bucket is a `std::vector` of `std::unique_ptr<Tool>`. Insertion and lookup both use one bucket, and collisions simply add entries to its chain. I prefer this simplicity to linear probing, where neighboring occupied slots can form clusters and lengthen searches. Linear probing also needs a stopping condition when the table is full; chaining can store multiple entries per bucket.

The hash follows the specification’s 64-bit FNV-1a implementation, processing every byte in order. This distinguishes similarly named tools. The bucket index is `hash & (bucket_count - 1)`, equivalent to modulo because bucket counts are powers of two.

The registry starts with 16 buckets and checks whether the next insertion would exceed a load factor of 1.0. If so, it doubles the bucket count and redistributes every entry using its new index. With a well-distributed hash, the average chain length is approximately the load factor, so 1.0 is reasonable for chaining.

Measurements for 10,000 names, `tool_0` through `tool_9999`:

| Hash | Buckets | Load factor | Longest chain |
|---|---|---|---|
| FNV-1a | 16384 | 0.6104 | 6 |
| djb2 | 16384 | 0.6104 | 4 |
| `s[0] + s.size()` | 16384 | 0.6104 | 9000 |

The weak hash sees the same initial `t` and only four possible lengths. The 9,000 four-digit names therefore share one bucket, making searches there linear in the chain length. djb2 performs better here, but is not necessarily better generally.

There are ten doublings to reach 16,384 buckets. Each resize costs linear work, but the number of insertions between resizes grows geometrically. Summing all redistribution work gives O(n) across n insertions, or O(1) amortized overhead per insertion. Expected constant-time insertion also assumes a good hash distribution and bounded name lengths.

## 2. Pointer stability

Buckets own heap-allocated tools through `unique_ptr`. Rehashing moves these owning pointers between buckets without relocating the tools themselves. Consequently, a `Tool*` returned by `find()` keeps the same address across resizes while the registry owns that tool. Storing objects directly in vectors could invalidate pointers during growth. The `reg_10000_tools` and `reg_pointers_stable_across_resizes` tests check saved addresses after growth.

## 3. Calculator trace

For `2 * (3 + 4) - 5`, both stacks below run from bottom to top.

| Token | Action | values | ops |
|---|---|---|---|
| `2` | Store the number | `2` | empty |
| `*` | Save multiplication | `2` | `*` |
| `(` | Mark the group boundary | `2` | `* (` |
| `3` | Store the number | `2 3` | `* (` |
| `+` | Save addition above the boundary | `2 3` | `* ( +` |
| `4` | Store the number | `2 3 4` | `* ( +` |
| `)` | Add 3 and 4; discard the matching opening parenthesis | `2 7` | `*` |
| `-` | Multiply 2 by 7; save subtraction | `14` | `-` |
| `5` | Store the number | `14 5` | `-` |
| end | Subtract 5 from 14 | `9` | empty |

The closing parenthesis finishes the enclosed operations before outside operators use their result. When subtraction arrives, multiplication runs first because its precedence is higher. The `>=` comparison also applies equal-precedence operators from left to right. Each operation pops the right operand first, then the left; reversing them would break subtraction and division.

## 4. Errors as values

Missing files, forbidden paths, and division by zero are expected failures in an agent loop. Returning `false` with an error message or `ToolResult::failure` lets the caller turn failure into conversation data without unwinding through the dispatcher. For example, dividing by zero produces an `"ok":false` result that the model can read before responding again. Calling `as_string()` on a JSON number instead indicates incorrect API use and throws `std::logic_error`. The dispatcher catches exceptions as a final safeguard.

## 5. What I would design differently

I would cache each tool’s name and hash with its owning pointer. Searching a chain repeatedly calls virtual `name()`, which returns a string, and rehashing repeats hashing. Cached keys would reduce repetitive work but costs extra storage and would require treating registered names as fixed.
