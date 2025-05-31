// background.js
// Initialize context menu items
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "log-url",
    title: "Log this URL",
    contexts: ["page"]
  });
  
  // chrome.contextMenus.create({
  //   id: "download-log",
  //   title: "Download URL log",
  //   contexts: ["page"]
  // });

  // Display a notification about the hotkeys when extension is installed
  //chrome.tabs.create({
  //  url: "onboarding.html"
  //});
});

/*
// Function to log the current URL
function logCurrentUrl() {
  chrome.tabs.query({active: true, currentWindow: true}, function(tabs) {
    if (tabs.length === 0) return;
    
    const currentUrl = tabs[0].url;
    const tabTitle = tabs[0].title || 'No title';
    const timestamp = new Date().toISOString();
    const logEntry = `${timestamp} - ${tabTitle} - ${currentUrl}\n`;
    
    // Get existing log from storage
    chrome.storage.local.get(['urlLog'], function(result) {
      let urlLog = result.urlLog || '';
      urlLog += logEntry;
      
      // Save updated log to storage
      chrome.storage.local.set({urlLog: urlLog}, function() {
        // Show a brief notification
        chrome.action.setBadgeText({text: "✓"});
        chrome.action.setBadgeBackgroundColor({color: "#4CAF50"});
        
        // Clear notification after 1.5 seconds
        setTimeout(() => {
          chrome.action.setBadgeText({text: ""});
        }, 1500);
      });
    });
  });
}

// Function to download the log file
function downloadLogFile() {
  chrome.storage.local.get(['urlLog'], function(result) {
    if (!result.urlLog) {
      chrome.action.setBadgeText({text: "!"});
      chrome.action.setBadgeBackgroundColor({color: "#F44336"});
      
      // Clear notification after 1.5 seconds
      setTimeout(() => {
        chrome.action.setBadgeText({text: ""});
      }, 1500);
      return;
    }
    
    const blob = new Blob([result.urlLog], {type: 'text/plain'});
    const url = URL.createObjectURL(blob);
    const filename = 'url_log_' + new Date().toISOString().slice(0,10) + '.txt';
    
    chrome.downloads.download({
      url: url,
      filename: filename,
      saveAs: true
    }, function() {
      chrome.action.setBadgeText({text: "✓"});
      chrome.action.setBadgeBackgroundColor({color: "#4CAF50"});
      
      // Clear notification after 1.5 seconds
      setTimeout(() => {
        chrome.action.setBadgeText({text: ""});
      }, 1500);
    });
  });

  //chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  //  if (message.action === "createBlob") {
  //      sendResponse({ error: "Cannot create Blob URL in a service worker. Use a content script." });
  //  }
  //});
}
*/

function logCurrentUrl() {
  chrome.tabs.query({active: true, currentWindow: true}, function(tabs) {
    if (tabs.length > 0) {
      const currentUrl = tabs[0].url;
      const tabTitle = tabs[0].title || 'No title';
      
      // Log to browser console
      console.log(`Current URL: ${currentUrl}`);
      console.log(`Page Title: ${tabTitle}`);
      
      // Show brief notification on the icon
      chrome.action.setBadgeText({text: "✓"});
      chrome.action.setBadgeBackgroundColor({color: "#4CAF50"});
      
      // Clear notification after 1.5 seconds
      setTimeout(() => {
        chrome.action.setBadgeText({text: ""});
      }, 1500);
    }
  });
}

chrome.commands.onCommand.addListener((command) => {
  if (command === "log-current-url") {
    logCurrentUrl();
  }
});

// Listen for keyboard shortcuts
// chrome.commands.onCommand.addListener((command) => {
//   if (command === "log-current-url") {
//     logCurrentUrl();
//   } else if (command === "download-url-log") {
//     downloadLogFile();
//   }
// });


// // Listen for context menu clicks
// chrome.contextMenus.onClicked.addListener((info, tab) => {
//   if (info.menuItemId === "log-url") {
//     logCurrentUrl();
//   } else if (info.menuItemId === "download-log") {
//     downloadLogFile();
//   }
// });



// Listen for toolbar icon clicks
chrome.action.onClicked.addListener(() => {
  logCurrentUrl();
});
