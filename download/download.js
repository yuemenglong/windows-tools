(async () => {
    const videoUrl = ""

    try {
        // 1. 获取文件内容
        const response = await fetch(videoUrl);
        if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}`);

        // 2. 转为Blob
        const blob = await response.blob();

        // 3. 创建本地下载链接
        const blobUrl = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = blobUrl;
        a.download = "video.mp4";  // 自定义保存的文件名
        document.body.appendChild(a);
        a.click();

        // 4. 清理
        URL.revokeObjectURL(blobUrl);
        a.remove();
        console.log("Download initiated.");

    } catch (error) {
        console.error("Download failed:", error);
    }
})();
