import subprocess

def run_edge_sobel(args):
    try:
        # تشغيل البرنامج التنفيذي
        result = subprocess.run(
            args,
            capture_output=True,  # عشان ناخد stdout و stderr
            text=True,
            check=True
        )
        # سجل المخرجات الناجحة
        with open("edge_sobel_log.txt", "a") as f:
            f.write(f"SUCCESS:\n{result.stdout}\n")
        return True
    except subprocess.CalledProcessError as e:
        # سجل أي خطأ
        with open("edge_sobel_log.txt", "a") as f:
            f.write(f"ERROR:\n{e.stderr}\n")
        return False
