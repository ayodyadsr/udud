# vhs demo.tape -> demo/xcull.gif
# install vhs: https://github.com/charmbracelet/vhs

Output demo/xcull.gif

Set FontSize 16
Set Width 1200
Set Height 720
Set Theme "Dracula"
Set Padding 18
Set TypingSpeed 35ms
Set PlaybackSpeed 1.0

Type "# raw recon dump (51 URLs)"
Enter
Sleep 600ms
Type "wc -l demo/urls.txt"
Enter
Sleep 1s

Type "# clean it"
Enter
Sleep 400ms
Type "cat demo/urls.txt | xcull -V > clean.txt"
Enter
Sleep 1500ms

Type "wc -l clean.txt"
Enter
Sleep 1500ms

Type "# distinct user IDs survive (IDOR / BOLA surface intact)"
Enter
Sleep 400ms
Type "grep '/api/v1/users/' clean.txt"
Enter
Sleep 2s

Type "# distinct UUID orders survive too"
Enter
Sleep 400ms
Type "grep '/api/v1/orders/' clean.txt"
Enter
Sleep 2s

Type "# every distinct jsessionid survives for authz testing"
Enter
Sleep 400ms
Type "grep 'jsessionid' clean.txt"
Enter
Sleep 2500ms

Type "# images, fonts, css and tracker URLs are dropped"
Enter
Sleep 400ms
Type "grep -E '\\.(png|css|woff2|svg|jpg)$|utm_source=twitter' clean.txt | wc -l"
Enter
Sleep 2s

Type "# admin/debug/backup endpoints all survive"
Enter
Sleep 400ms
Type "grep -E 'admin|debug|backup|\\.env' clean.txt"
Enter
Sleep 3s
