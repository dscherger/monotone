/*

 ============================================================================
                   The Apache Software License, Version 1.1
 ============================================================================

 Copyright (C) 1999-2003 The Apache Software Foundation. All rights reserved.

 Redistribution and use in source and binary forms, with or without modifica-
 tion, are permitted provided that the following conditions are met:

 1. Redistributions of  source code must  retain the above copyright  notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 3. The end-user documentation included with the redistribution, if any, must
    include  the following  acknowledgment:  "This product includes  software
    developed  by the  Apache Software Foundation  (http://www.apache.org/)."
    Alternately, this  acknowledgment may  appear in the software itself,  if
    and wherever such third-party acknowledgments normally appear.

 4. The names "Batik" and  "Apache Software Foundation" must  not  be
    used to  endorse or promote  products derived from  this software without
    prior written permission. For written permission, please contact
    apache@apache.org.

 5. Products  derived from this software may not  be called "Apache", nor may
    "Apache" appear  in their name,  without prior written permission  of the
    Apache Software Foundation.

 THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED WARRANTIES,
 INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 FITNESS  FOR A PARTICULAR  PURPOSE ARE  DISCLAIMED.  IN NO  EVENT SHALL  THE
 APACHE SOFTWARE  FOUNDATION  OR ITS CONTRIBUTORS  BE LIABLE FOR  ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL,  EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLU-
 DING, BUT NOT LIMITED TO, PROCUREMENT  OF SUBSTITUTE GOODS OR SERVICES; LOSS
 OF USE, DATA, OR  PROFITS; OR BUSINESS  INTERRUPTION)  HOWEVER CAUSED AND ON
 ANY  THEORY OF LIABILITY,  WHETHER  IN CONTRACT,  STRICT LIABILITY,  OR TORT
 (INCLUDING  NEGLIGENCE OR  OTHERWISE) ARISING IN  ANY WAY OUT OF THE  USE OF
 THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 This software  consists of voluntary contributions made  by many individuals
 on  behalf of the Apache Software  Foundation. For more  information on the
 Apache Software Foundation, please see <http://www.apache.org/>.

*/

package com.untrusted.script;

import org.apache.batik.script.ScriptHandler;
import org.apache.batik.script.Window;

import org.apache.batik.dom.svg.SVGOMDocument;

import org.w3c.dom.*;
import org.w3c.dom.events.*;

import java.awt.AWTPermission;
import java.io.FilePermission;
import java.io.SerializablePermission;
import java.lang.reflect.ReflectPermission;
import java.net.NetPermission;
import java.net.SocketPermission;
import java.net.URL;
import java.security.AllPermission;
import java.security.Permission;
import java.security.SecurityPermission;
import java.sql.SQLPermission;
import java.util.PropertyPermission;
import javax.sound.sampled.AudioPermission;

/**
 * This class implements the ScriptHandler interface and represents an 
 * example of untrusted code.
 *
 * It creates a number of Java Permissions and checks that access is denied.
 * the tests fail if the Permissions are granted.
 *
 * The only thing that the class should be allowed to make is a connection
 * back to the server that served the document containing this script.
 *
 * @author <a href="mailto:vhardy@apache.org">Vincent Hardy</a>
 * @version $Id: UntrustedScriptHandler.java,v 1.3 2003/08/08 11:39:31 vhardy Exp $
 */
public class UntrustedScriptHandler implements ScriptHandler {
    public static final String svgNS = "http://www.w3.org/2000/svg";

    /**
     * Path for the file tested with FilePermission
     */
    public static final String testedPath = "build.sh";

    /**
     * Host which is used for testing
     */
    public static final String testedHost = "nagoya.apache.org:8080";

    /**
     * Table of Permissions which will be tested.
     */
    protected static Object[][] basePermissions = {
        {"AllPermission", new AllPermission()}, 
        {"FilePermission read", new FilePermission(testedPath, "read")}, 
        {"FilePermission write", new FilePermission(testedPath, "write")}, 
        {"FilePermission execute", new FilePermission(testedPath, "execute")}, 
        {"FilePermission delete", new FilePermission(testedPath, "delete")}, 
        // 1.4 {"ServicePermission", new ServicePermission("krbtgt/EXAMPLE.COM@EXAMPLE.COM", "initiate")}, 
        {"SocketPermission accept", new SocketPermission(testedHost, "accept")}, 
        {"SocketPermission connect", new SocketPermission(testedHost, "connect")}, 
        {"SocketPermission listen", new SocketPermission(testedHost, "listen")}, 
        {"SocketPermission resolve", new SocketPermission(testedHost, "resolve")}, 
        {"AudioPermission play", new AudioPermission("play")}, 
        {"AudioPermission record", new AudioPermission("record")}, 
        {"AWTPermission accessClipboard", new AWTPermission("accessClipboard")}, 
        {"AWTPermission accessEventQueue", new AWTPermission("accessEventQueue")}, 
        {"AWTPermission listenToAllAWTEvents", new AWTPermission("listenToAllAWTEvents")}, 
        {"AWTPermission showWindowWithoutWarningBanner", new AWTPermission("showWindowWithoutWarningBanner")}, 
        {"AWTPermission readDisplayPixels", new AWTPermission("readDisplayPixels")}, 
        {"AWTPermission createRobot", new AWTPermission("createRobot")}, 
        {"AWTPermission fullScreenExclusive", new AWTPermission("fullScreenExclusive")}, 
        // 1.4 {"DelegationPermission", new DelegationPermission()}, 
        // 1.4 {"LoggingPermission", new LoggingPermission("control")}, 
        {"NetPermission setDefaultAuthenticator", new NetPermission("setDefaultAuthenticator")}, 
        {"NetPermission requestPasswordAuthentication", new NetPermission("requestPasswordAuthentication")}, 
        {"NetPermission specifyStreamHandler", new NetPermission("specifyStreamHandler")}, 
        {"PropertyPermission java.home read", new PropertyPermission("java.home", "read")}, 
        {"PropertyPermission java.home write", new PropertyPermission("java.home", "write")}, 
        {"ReflectPermission", new ReflectPermission("suppressAccessChecks")}, 
        {"RuntimePermission createClassLoader", new RuntimePermission("createClassLoader")}, 
        {"RuntimePermission getClassLoader", new RuntimePermission("getClassLoader")}, 
        {"RuntimePermission setContextClassLoader", new RuntimePermission("setContextClassLoader")}, 
        {"RuntimePermission setSecurityManager", new RuntimePermission("setSecurityManager")}, 
        {"RuntimePermission createSecurityManager", new RuntimePermission("createSecurityManager")}, 
        {"RuntimePermission exitVM", new RuntimePermission("exitVM")}, 
        {"RuntimePermission shutdownHooks", new RuntimePermission("shutdownHooks")}, 
        {"RuntimePermission setFactory", new RuntimePermission("setFactory")}, 
        {"RuntimePermission setIO", new RuntimePermission("setIO")}, 
        {"RuntimePermission modifyThread", new RuntimePermission("modifyThread")}, 
        {"RuntimePermission stopThread", new RuntimePermission("stopThread")}, 
        {"RuntimePermission modifyThreadGroup", new RuntimePermission("modifyThreadGroup")}, 
        {"RuntimePermission getProtectionDomain", new RuntimePermission("getProtectionDomain")}, 
        {"RuntimePermission readFileDescriptor", new RuntimePermission("readFileDescriptor")}, 
        {"RuntimePermission writeFileDescriptor", new RuntimePermission("writeFileDescriptor")}, 
        {"RuntimePermission loadLibrary.{library name}", new RuntimePermission("loadLibrary.{library name}")}, 
        {"RuntimePermission accessClassInPackage.java.security", new RuntimePermission("accessClassInPackage.java.security")}, 
        {"RuntimePermission defineClassInPackage.java.lang", new RuntimePermission("defineClassInPackage.java.lang")}, 
        {"RuntimePermission accessDeclaredMembers", new RuntimePermission("accessDeclaredMembers")}, 
        {"RuntimePermission queuePrintJob", new RuntimePermission("queuePrintJob")}, 

        {"SecurityPermission createAccessControlContext", new SerializablePermission("createAccessControlContext")}, 
        {"SecurityPermission getDomainCombiner", new SerializablePermission("getDomainCombiner")}, 
        {"SecurityPermission getPolicy", new SerializablePermission("getPolicy")}, 
        {"SecurityPermission setPolicy", new SerializablePermission("setPolicy")}, 
        {"SecurityPermission setSystemScope", new SerializablePermission("setSystemScope")}, 
        {"SecurityPermission setIdentityPublicKey", new SerializablePermission("setIdentityPublicKey")}, 
        {"SecurityPermission setIdentityInfo", new SerializablePermission("setIdentityInfo")}, 
        {"SecurityPermission addIdentityCertificate", new SerializablePermission("addIdentityCertificate")}, 
        {"SecurityPermission removeIdentityCertificate", new SerializablePermission("removeIdentityCertificate")}, 
        {"SecurityPermission printIdentity", new SerializablePermission("printIdentity")}, 
        {"SecurityPermission getSignerPrivateKey", new SerializablePermission("getSignerPrivateKey")}, 
        {"SecurityPermission setSignerKeyPair", new SerializablePermission("setSignerKeyPair")}, 

        {"SerializablePermission enableSubclassImplementation", new SerializablePermission("enableSubclassImplementation")},
        {"SerializablePermission enableSubstitution", new SerializablePermission("enableSubstitution")},

        {"SQLPermission", new SQLPermission("setLog")}, 

        // 1.4 {"SSLPermission setHostnameVerifier", new SSLPermission("setHostnameVerifier")}
        // 1.4{"SSLPermission getSSLSessionContext", new SSLPermission("getSSLSessionContext")}
    };
    
    /**
     * Set of Permissions to test. One is added if the Document is loaded from a host
     */
    private Object[][] permissions;

    /**
     * Reference to the rectangles which show the test status
     */
    private Element[] statusRects;

    /**
     * Runs this handler.  This method is called by the SVG viewer
     * when the scripts are loaded.
     * @param doc The current document.
     * @param win An object which represents the current viewer.
     */
    public void run(final Document doc, final Window win){
        int nGrantedTmp = 0;

        //
        // If the document is loaded over the network, check that the
        // class has permission to access the server
        //
        URL docURL = ((SVGOMDocument)doc).getURLObject();
        if (docURL != null && docURL.getHost() != null && !"".equals(docURL.getHost())) {
            permissions = new Object[basePermissions.length + 3][2];
            System.arraycopy(basePermissions, 0, 
                             permissions, 3, basePermissions.length);

            String docHost = docURL.getHost();
            if (docURL.getPort() != -1) {
                docHost += ":" + docURL.getPort();
            }

            permissions[0][0] = "SocketPermission accept " + docHost;
            permissions[0][1] = new SocketPermission(docHost, "accept");
            permissions[1][0] = "SocketPermission connect " + docHost;
            permissions[1][1] = new SocketPermission(docHost, "connect");
            permissions[2][0] = "SocketPermission resolve " + docHost;
            permissions[2][1] = new SocketPermission(docHost, "resolve");
            nGrantedTmp = 3;
        } else {
            permissions = basePermissions;
        }

        // Captures the number of permissions which should be 
        // granted to this code.
        final int nGranted = nGrantedTmp;

        //
        // Build a table in the scrollable area of the document
        //
        Element securityResults = doc.getElementById("securityResults");
        statusRects = new Element[permissions.length];

        for (int i=0; i<permissions.length; i++){
            Element textElt = doc.createElementNS(svgNS, "text");
            textElt.setAttributeNS(null, "x", "55");
            textElt.setAttributeNS(null, "y", "" + (85 + i*20));
            textElt.appendChild(doc.createTextNode(permissions[i][0].toString()));
            securityResults.appendChild(textElt);

            Element rectElt = doc.createElementNS(svgNS, "rect");
            rectElt.setAttributeNS(null, "x", "50");
            rectElt.setAttributeNS(null, "y", "" + (70 + i*20));
            rectElt.setAttributeNS(null, "width", "330");
            rectElt.setAttributeNS(null, "height", "20" );
            rectElt.setAttributeNS(null, "class", "tableCell");
            securityResults.appendChild(rectElt);

            rectElt = doc.createElementNS(svgNS, "rect");
            rectElt.setAttributeNS(null, "x", "380");
            rectElt.setAttributeNS(null, "y", "" + (70 + i*20));
            rectElt.setAttributeNS(null, "width", "20");
            rectElt.setAttributeNS(null, "height", "20" );
            rectElt.setAttributeNS(null, "class", "tableCell");
            securityResults.appendChild(rectElt);

            rectElt = doc.createElementNS(svgNS, "rect");
            rectElt.setAttributeNS(null, "x", "383");
            rectElt.setAttributeNS(null, "y", "" + (73 + i*20));
            rectElt.setAttributeNS(null, "width", "14");
            rectElt.setAttributeNS(null, "height", "14" );
            rectElt.setAttributeNS(null, "class", "untested");
            securityResults.appendChild(rectElt);

            statusRects[i] = rectElt;
        }

        EventTarget testButton = (EventTarget)doc.getElementById("runTest");
        testButton.addEventListener("click", new EventListener() {
                public void handleEvent(Event evt){
                    SecurityManager sm = System.getSecurityManager();
                    int successCnt = 0;

                    if (sm == null){
                        for (int i=0; i<nGranted; i++) {
                            statusRects[i].setAttributeNS(null, "class", "passedTest");
                            successCnt++;
                        }
                        for (int i=nGranted; i<permissions.length; i++) {
                            statusRects[i].setAttributeNS(null, "class", "failedTest");
                        }
                    }
                    else {
                        for (int i=0; i<nGranted; i++) {
                            Permission p = (Permission)permissions[i][1];
                            boolean success = true;
                            try {
                                sm.checkPermission(p);
                                statusRects[i].setAttributeNS(null, "class", "passedTest");
                                successCnt++;
                            } catch (SecurityException se){
                                statusRects[i].setAttributeNS(null, "class", "failedTest");
                                System.out.println("*********************************************");
                                se.printStackTrace();
                            }
                        }

                        for (int i=nGranted; i<permissions.length; i++) {
                            Permission p = (Permission)permissions[i][1];
                            boolean success = true;
                            try {
                                sm.checkPermission(p);
                                statusRects[i].setAttributeNS(null, "class", "failedTest");
                            } catch (SecurityException se){
                                statusRects[i].setAttributeNS(null, "class", "passedTest");
                                successCnt++;
                            }
                        }

                    }

                    // Update the global status
                    Element globalStatus = doc.getElementById("globalStatus");
                    if ( successCnt == (statusRects.length) ) {
                        globalStatus.setAttributeNS(null, "class", "passedTest");
                    } else {
                        globalStatus.setAttributeNS(null, "class", "failedTest");
                    }
                    
                    String successRatioString = "Test Result: " + successCnt + " / " + statusRects.length;
                    Element successRatio = doc.getElementById("successRatio");
                    successRatio.replaceChild(doc.createTextNode(successRatioString),
                                              successRatio.getFirstChild());
                    
                }
            }, false);

        
    }

}

