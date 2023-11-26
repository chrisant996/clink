# Lua Notes

## Performance

### Implicit boolean vs equality comparison (100,000,000 iterations).
- `if mode then` -> 1.23 sec.
- `if mode == true then` -> 1.42 sec.
- `if mode == "one" then` -> 1.44 sec.

### True vs false branch path (100,000,000 iterations).
- `if x then` **false** branch 0.2 sec **faster** than true branch.
- `not` is insignificant; negligible difference between `if x then` vs `if not x then` when following the same branch path.

### Cache table value in local var (100,000,000 iterations).
- `x = x + t.longer_name.pinky` unconditional -> 2.0 sec.
- `x = x + t.longer_name.pinky` true branch path w/no else block -> 2.4 sec.
- `x = x + t.longer_name.pinky` true branch path w/an else block -> 2.7 sec.
- `x = x + t.longer_name.pinky` false branch path -> 2.5 sec.
- `x = x + cached` unconditional -> 0.68 sec.
- `x = x + cached` true branch path w/no else block -> 0.98 sec.
- `x = x + cached` true branch path w/an else block -> 1.24 sec.
- `x = x + cached` false branch path -> 1.18 sec.

