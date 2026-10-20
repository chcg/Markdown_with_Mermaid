# Security fixture

<script>window.INJECTED = 1; chrome.webview.postMessage('close')</script>
<img src="missing.png" onerror="window.INJECTED = 2">
<iframe src="https://example.com"></iframe>
<a href="javascript:window.INJECTED=3">bad link</a>
<style>body{display:none}</style>
<form action="https://example.com"><input name="password"></form>

```html
</script><script>window.INJECTED = 4</script>
```

__MV_NONCE__ __MV_ASSET_BASE__ __MV_PAYLOAD__

Safe text still visible.
