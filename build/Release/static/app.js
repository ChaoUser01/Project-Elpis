// Elpis — Frontend Logic (v2.0 — Sovereign Ledger + BYOK)

(function () {
    'use strict';

    // ── DOM refs ───────────────────────────────────────────────────────────
    const authOverlay    = document.getElementById('authOverlay');
    const appContainer   = document.getElementById('appContainer');
    const authError      = document.getElementById('authError');
    const tabLogin       = document.getElementById('tabLogin');
    const tabClaim       = document.getElementById('tabClaim');
    const loginForm      = document.getElementById('loginForm');
    const claimForm      = document.getElementById('claimForm');
    const loginStudentId = document.getElementById('loginStudentId');
    const claimStudentId = document.getElementById('claimStudentId');
    const userNameEl     = document.getElementById('userName');
    const logoutBtn      = document.getElementById('logoutBtn');
    const promptInput    = document.getElementById('promptInput');
    const charCount      = document.getElementById('charCount');
    const dropzone       = document.getElementById('dropzone');
    const dropzoneContent= document.getElementById('dropzoneContent');
    const dropzonePreview= document.getElementById('dropzonePreview');
    const fileInput      = document.getElementById('fileInput');
    const previewImg     = document.getElementById('previewImg');
    const previewName    = document.getElementById('previewName');
    const previewRemove  = document.getElementById('previewRemove');
    const generateBtn    = document.getElementById('generateBtn');
    const btnStatus      = document.getElementById('btnStatus');
    const resultCard     = document.getElementById('resultCard');
    const resultTitle    = document.getElementById('resultTitle');
    const resultTime     = document.getElementById('resultTime');
    const resultSession  = document.getElementById('resultSession');
    const downloadBtn    = document.getElementById('downloadBtn');
    const toastContainer = document.getElementById('toastContainer');

    let selectedFile = null;
    let authToken = sessionStorage.getItem('elpis_token') || '';
    let currentUser = sessionStorage.getItem('elpis_user') || '';

    // ── Auth State Management ─────────────────────────────────────────────

    function showAuth() {
        authOverlay.style.display = 'flex';
        appContainer.style.display = 'none';
        loadStudentList();
    }

    function showApp(name) {
        authOverlay.style.display = 'none';
        appContainer.style.display = 'block';
        userNameEl.textContent = name;
    }

    function showAuthError(msg) {
        authError.textContent = msg;
        authError.style.display = 'block';
        setTimeout(() => { authError.style.display = 'none'; }, 5000);
    }

    // ── Load student list into dropdowns ───────────────────────────────────

    let studentsData = [];

    async function loadStudentList() {
        try {
            const res = await fetch('/api/students');
            studentsData = await res.json();
            [loginStudentId, claimStudentId].forEach(select => {
                select.innerHTML = '<option value="" disabled selected>Select your Student ID</option>';
                studentsData.forEach(s => {
                    const opt = document.createElement('option');
                    opt.value = s.id;
                    opt.textContent = `${s.id} — ${s.name}`;
                    if (select === claimStudentId && s.claimed) {
                        opt.disabled = true;
                        opt.textContent += ' (claimed)';
                    }
                    select.appendChild(opt);
                });
            });
        } catch (e) {
            console.error('Failed to load student list', e);
        }
    }

    // When login student dropdown changes, update API key hint
    loginStudentId.addEventListener('change', () => {
        const s = studentsData.find(x => x.id === loginStudentId.value);
        const apiKeyInput = document.getElementById('loginApiKey');
        if (s && s.hasSavedKey) {
            apiKeyInput.required = false;
            apiKeyInput.placeholder = 'Leave blank to use saved key';
        } else {
            apiKeyInput.required = true;
            apiKeyInput.placeholder = 'Your Groq or Gemini API key';
        }
    });

    // ── Tab switching ─────────────────────────────────────────────────────

    tabLogin.addEventListener('click', () => {
        tabLogin.classList.add('active');
        tabClaim.classList.remove('active');
        loginForm.style.display = '';
        claimForm.style.display = 'none';
        authError.style.display = 'none';
    });

    tabClaim.addEventListener('click', () => {
        tabClaim.classList.add('active');
        tabLogin.classList.remove('active');
        claimForm.style.display = '';
        loginForm.style.display = 'none';
        authError.style.display = 'none';
    });

    // ── Claim form ────────────────────────────────────────────────────────

    claimForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const studentId = claimStudentId.value;
        const pass = document.getElementById('claimPass').value;
        const confirm = document.getElementById('claimPassConfirm').value;

        if (pass !== confirm) {
            showAuthError('Passphrases do not match.');
            return;
        }

        try {
            const res = await fetch('/api/claim', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ student_id: studentId, passphrase: pass })
            });
            const data = await res.json();
            if (!res.ok) {
                showAuthError(data.error || 'Claim failed.');
                return;
            }
            showToast(data.message || 'Account claimed! Please sign in.', 'success');
            // Switch to login tab
            tabLogin.click();
            loginStudentId.value = studentId;
            loadStudentList();
        } catch (err) {
            showAuthError('Network error. Is the server running?');
        }
    });

    // ── Login form ────────────────────────────────────────────────────────

    loginForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const studentId = loginStudentId.value;
        const pass = document.getElementById('loginPass').value;
        const apiKey = document.getElementById('loginApiKey').value;

        try {
            const res = await fetch('/api/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ student_id: studentId, passphrase: pass, api_key: apiKey })
            });
            const data = await res.json();
            if (!res.ok) {
                showAuthError(data.error || 'Login failed.');
                return;
            }
            authToken = data.token;
            currentUser = data.name;
            sessionStorage.setItem('elpis_token', authToken);
            sessionStorage.setItem('elpis_user', currentUser);
            showApp(currentUser);
            showToast(`Welcome, ${currentUser}!`, 'success');
        } catch (err) {
            showAuthError('Network error. Is the server running?');
        }
    });

    // ── Logout ────────────────────────────────────────────────────────────

    logoutBtn.addEventListener('click', async () => {
        try {
            await fetch('/api/logout', {
                method: 'POST',
                headers: { 'Authorization': 'Bearer ' + authToken }
            });
        } catch {}
        authToken = '';
        currentUser = '';
        sessionStorage.removeItem('elpis_token');
        sessionStorage.removeItem('elpis_user');
        showAuth();
        showToast('Signed out successfully.', 'info');
    });

    // ── Session check on load ─────────────────────────────────────────────

    async function checkSession() {
        if (!authToken) { showAuth(); return; }

        try {
            const res = await fetch('/api/me', {
                headers: { 'Authorization': 'Bearer ' + authToken }
            });
            if (res.ok) {
                const data = await res.json();
                currentUser = data.name;
                sessionStorage.setItem('elpis_user', currentUser);
                showApp(currentUser);
            } else {
                // Session expired
                sessionStorage.removeItem('elpis_token');
                sessionStorage.removeItem('elpis_user');
                authToken = '';
                showAuth();
            }
        } catch {
            showAuth();
        }
    }

    // ── Character counter ──────────────────────────────────────────────────
    promptInput.addEventListener('input', () => {
        const len = promptInput.value.length;
        charCount.textContent = len + ' character' + (len !== 1 ? 's' : '');
    });

    // ── Dropzone ───────────────────────────────────────────────────────────
    dropzone.addEventListener('click', () => fileInput.click());

    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
    });

    dropzone.addEventListener('dragleave', () => {
        dropzone.classList.remove('dragover');
    });

    dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        const files = e.dataTransfer.files;
        if (files.length > 0) handleFile(files[0]);
    });

    fileInput.addEventListener('change', () => {
        if (fileInput.files.length > 0) handleFile(fileInput.files[0]);
    });

    previewRemove.addEventListener('click', (e) => {
        e.stopPropagation();
        clearFile();
    });

    function handleFile(file) {
        if (!file.type.startsWith('image/')) {
            showToast('Please upload a JPEG or PNG image.', 'error');
            return;
        }
        if (file.size > 10 * 1024 * 1024) {
            showToast('File too large. Maximum size is 10MB.', 'error');
            return;
        }

        selectedFile = file;
        previewName.textContent = file.name;

        const reader = new FileReader();
        reader.onload = (e) => {
            previewImg.src = e.target.result;
            dropzoneContent.style.display = 'none';
            dropzonePreview.style.display = 'flex';
        };
        reader.readAsDataURL(file);
    }

    function clearFile() {
        selectedFile = null;
        fileInput.value = '';
        previewImg.src = '';
        previewName.textContent = '';
        dropzoneContent.style.display = '';
        dropzonePreview.style.display = 'none';
    }

    // ── Generate ───────────────────────────────────────────────────────────
    generateBtn.addEventListener('click', async () => {
        const prompt = promptInput.value.trim();

        if (!prompt && !selectedFile) {
            showToast('Please enter a prompt or upload an image.', 'error');
            promptInput.focus();
            return;
        }

        // Set loading state
        generateBtn.classList.add('loading');
        generateBtn.disabled = true;
        btnStatus.textContent = 'Analysing prompt...';
        resultCard.style.display = 'none';

        try {
            // Build form data
            const formData = new FormData();
            if (prompt) formData.append('prompt', prompt);
            if (selectedFile) formData.append('file', selectedFile);

            const startTime = performance.now();

            const headers = {};
            if (authToken) {
                headers['Authorization'] = 'Bearer ' + authToken;
            }

            const response = await fetch('/generate', {
                method: 'POST',
                body: formData,
                headers: headers,
            });

            if (!response.ok) {
                let errMsg = 'Generation failed to start';
                try {
                    const err = await response.json();
                    errMsg = err.error || errMsg;
                } catch {}
                throw new Error(errMsg);
            }

            const { sessionId } = await response.json();
            
            // Connect to EventSource
            const es = new EventSource(`/api/stream?session_id=${sessionId}`);
            
            es.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    btnStatus.textContent = data.status || 'Generating...';
                    
                    if (data.isError) {
                        es.close();
                        showToast(data.status || 'An error occurred during generation.', 'error');
                        generateBtn.classList.remove('loading');
                        generateBtn.disabled = false;
                    } else if (data.isComplete) {
                        es.close();
                        const elapsed = ((performance.now() - startTime) / 1000).toFixed(2);
                        
                        // Show result
                        resultTitle.textContent = data.reportTitle || 'Academic Report';
                        resultTime.textContent = elapsed + ' s';
                        
                        // PDF preview
                        if (data.pdfUrl) {
                            pdfViewer.innerHTML = `<iframe src="${data.pdfUrl}#toolbar=0" width="100%" height="800px" style="border:none; border-radius:8px;"></iframe>`;
                            downloadPdf.href = data.pdfUrl;
                            
                            // Extract filename for download
                            const parts = data.pdfUrl.split('/');
                            const filename = parts[parts.length - 1] || 'report.pdf';
                            downloadBtn.download = filename;
                            downloadBtn.href = data.pdfUrl;
                        }

                        resultSession.textContent = sessionId.substring(0, 12) + '...';
                        resultCard.style.display = 'block';
                        showToast('Report generated successfully!', 'success');

                        // NEW: Fetch and render assets
                        fetchAssets(sessionId);

                        generateBtn.classList.remove('loading');
                        generateBtn.disabled = false;
                    }
                } catch (e) {
                    console.error('Failed to parse SSE event', e);
                }
            };

            async function fetchAssets(sid) {
                try {
                    const res = await fetch(`/api/assets?session_id=${sid}`);
                    const assets = await res.json();

                    const assetsCard = document.getElementById('assetsCard');
                    const assetsList = document.getElementById('assetsList');
                    
                    if (assets && assets.length > 0) {
                        assetsCard.style.display = 'block';
                        assetsList.innerHTML = '';
                        
                        assets.forEach(filename => {
                            const ext = filename.split('.').pop().toLowerCase();
                            let icon = 'fa-file-code';
                            let lang = 'CODE';
                            
                            if (ext === 'py') { icon = 'fa-brands fa-python'; lang = 'Python'; }
                            else if (ext === 'js') { icon = 'fa-brands fa-js'; lang = 'JavaScript'; }
                            else if (ext === 'cpp' || ext === 'h') { icon = 'fa-file-code'; lang = 'C++'; }
                            else if (ext === 'html') { icon = 'fa-brands fa-html5'; lang = 'HTML'; }
                            else if (ext === 'css') { icon = 'fa-brands fa-css3-alt'; lang = 'CSS'; }

                            const card = document.createElement('div');
                            card.className = 'asset-card';
                            card.innerHTML = `
                                <div class="asset-icon"><i class="fas ${icon}"></i></div>
                                <div class="asset-name">${filename}</div>
                                <div class="asset-lang">${lang}</div>
                                <a href="/api/download?session_id=${sid}&file=${filename}" class="btn-asset">
                                    <i class="fas fa-download"></i> Download
                                </a>
                            `;
                            assetsList.appendChild(card);
                        });
                    } else {
                        assetsCard.style.display = 'none';
                    }
                } catch (e) {
                    console.error('Failed to fetch assets', e);
                }
            }
            
            es.onerror = (err) => {
                es.close();
                showToast('Stream connection lost.', 'error');
                generateBtn.classList.remove('loading');
                generateBtn.disabled = false;
            };

        } catch (err) {
            showToast(err.message || 'An error occurred during generation.', 'error');
            generateBtn.classList.remove('loading');
            generateBtn.disabled = false;
        }
    });

    // ── Toasts ─────────────────────────────────────────────────────────────
    function showToast(message, type = 'info') {
        const toast = document.createElement('div');
        toast.className = 'toast toast--' + type;
        toast.textContent = message;
        toastContainer.appendChild(toast);

        setTimeout(() => {
            toast.classList.add('removing');
            setTimeout(() => toast.remove(), 300);
        }, 4500);
    }

    // ── Keyboard shortcut ──────────────────────────────────────────────────
    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            generateBtn.click();
        }
    });

    // ── Init ───────────────────────────────────────────────────────────────
    checkSession();

})();
