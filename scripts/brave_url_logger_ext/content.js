chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message.action === "createBlob") {
        try {
            // Create a simple Blob containing text
            const blob = new Blob(["Hello, Brave!"], { type: "text/plain" });

            // Generate a Blob URL (this only works in content scripts)
            const url = URL.createObjectURL(blob);

            console.log("Generated Blob URL:", url);

            // Send the Blob URL back to the sender
            sendResponse({ blobUrl: url });
        } catch (error) {
            console.error("Error creating Blob URL:", error);
            sendResponse({ error: error.message });
        }

        // Return true to indicate we will send a response asynchronously
        return true;
    }
});