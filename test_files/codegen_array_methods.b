extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
    Array<int> nums = [10, 20, 30];

    // Test length property
    int len = nums.length;
    printf("length=%d\n", len);
    if len != 3 {
        return 1;
    }

    // Test is_empty property
    bool e1 = nums.is_empty;
    if e1 {
        return 2;
    }

    // Test push
    nums.push(40);
    int len2 = nums.length;
    printf("after push length=%d\n", len2);
    if len2 != 4 {
        return 3;
    }

    int fourth = nums[3];
    printf("fourth=%d\n", fourth);
    if fourth != 40 {
        return 4;
    }

    // Test pop
    int popped = nums.pop();
    printf("popped=%d\n", popped);
    if popped != 40 {
        return 5;
    }

    int len3 = nums.length;
    if len3 != 3 {
        return 6;
    }

    return 0;
}
