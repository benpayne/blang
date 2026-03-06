extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
    Array<int> nums = [10, 20, 30];

    int first = nums[0];
    int second = nums[1];
    int third = nums[2];

    printf("first=%d second=%d third=%d\n", first, second, third);

    // Test for-in over array
    int sum = 0;
    for x in nums {
        sum = sum + x;
    }
    printf("sum=%d\n", sum);

    if sum != 60 {
        return 1;
    }

    return 0;
}
