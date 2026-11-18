#include <stdio.h>
#include <stdlib.h>

int main() {
    char name[100];
    char filename[120];
    int count;

    // دریافت نام از کاربر
    printf("لطفا نام خود را وارد کنید: ");
    scanf("%s", name);

    // ساخت نام فایل با پسوند txt (مثلاً ali.txt)
    snprintf(filename, sizeof(filename), "%s.txt", name);

    // باز کردن فایل در حالت نوشتن (Write)
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        printf("خطا در ایجاد فایل!\n");
        return 1;
    }

    // پرسیدن تعداد شماره‌ها
    printf("چند تا شماره؟ ");
    scanf("%d", &count);

    // نوشتن اعداد از ۱ تا عدد مورد نظر در فایل
    for (int i = 1; i <= count; i++) {
        fprintf(file, "%d\n", i);
    }

    // بستن فایل
    fclose(file);

    printf("فایل '%s' با موفقیت ایجاد شد و اعداد ۱ تا %d در آن ذخیره شدند.\n", filename, count);

    return 0;
}
