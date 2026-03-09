fn main() -> int {
    Array<int> nums = [10, 20, 30];

    // Test length property
    int len = nums.length;
    println("length={}", len);
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
    println("after push length={}", len2);
    if len2 != 4 {
        return 3;
    }

    int fourth = nums[3];
    println("fourth={}", fourth);
    if fourth != 40 {
        return 4;
    }

    // Test pop
    int popped = nums.pop();
    println("popped={}", popped);
    if popped != 40 {
        return 5;
    }

    int len3 = nums.length;
    if len3 != 3 {
        return 6;
    }

    return 0;
}
