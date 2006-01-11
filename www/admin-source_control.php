<div id="page">
<table><tr>
<td>
People allowed to write to the database:<br/>
(key name)<br/>
<textarea name="newperm" cols="35" rows="20" id="newperm"></textarea><br/>
<input type="submit" value="Change write permissions" onclick="chwperm();"/>
</td>

<td>
Upload a new pubkey.<br />
This is equivalent to someone with push access doing
<pre>mtn read file-with-key-packet
mtn --key-to-push=&lt;keyname&gt; push <?="$project.$hostname"?> ''</pre>
Pubkey packet:<br/>
<textarea name="keydata" id="keydata" cols="73" rows="10"></textarea><br/>
<input type="submit" value="Upload pubkey" onclick="sendkey();"/>
<hr/>
Current server status is &quot;<span id="serverstate">server state</span>&quot;<br/>
<input type="submit" value="Enable server" onclick="chstate();" id="chstate"/>
<input type="submit" value="Refresh" onclick="restate();" id="restate"/>
</td>
</tr></table>
<hr/>
This is to reset your database, for example if you had to do a rebuild. For this to work, you must type &quot;<tt>I solemnly swear that I have a backup.</tt>&quot; in the box below.<br/>
Note that Bad Things may happen if your server is running while you do this. (&quot;Current server status&quot; above should be SLEEPING or STOPPED or SHUTDOWN)<br/>
<input type="text" name="oath" id="oath" size="60"/>
<input type="submit" value="Reset database" onclick="resetdb();"/>
</div>
