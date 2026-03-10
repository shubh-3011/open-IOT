/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – Auth Page Logic (Login / Register)
   ═══════════════════════════════════════════════════════════════════════════ */

// Redirect if already logged in
if (getToken()) {
    window.location.href = '/dashboard';
}

// ── Tab Switching ─────────────────────────────────────────────────────────
function showTab(tab) {
    const loginForm = document.getElementById('login-form');
    const registerForm = document.getElementById('register-form');
    const tabLogin = document.getElementById('tab-login');
    const tabRegister = document.getElementById('tab-register');

    if (tab === 'login') {
        loginForm.classList.remove('hidden');
        registerForm.classList.add('hidden');
        tabLogin.classList.add('active');
        tabRegister.classList.remove('active');
    } else {
        loginForm.classList.add('hidden');
        registerForm.classList.remove('hidden');
        tabLogin.classList.remove('active');
        tabRegister.classList.add('active');
    }
}

// ── Login Handler ─────────────────────────────────────────────────────────
async function handleLogin(e) {
    e.preventDefault();
    const btn = document.getElementById('login-btn');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner"></div>';

    try {
        const username = document.getElementById('login-username').value;
        const password = document.getElementById('login-password').value;

        const data = await apiForm('/api/auth/login', { username, password });

        setToken(data.access_token);
        setUser(data.user);
        showToast('Welcome back!', 'success');

        setTimeout(() => {
            window.location.href = '/dashboard';
        }, 500);
    } catch (err) {
        showToast(err.message || 'Login failed', 'error');
        btn.disabled = false;
        btn.innerHTML = '<span>Sign In</span><span>→</span>';
    }
}

// ── Register Handler ──────────────────────────────────────────────────────
async function handleRegister(e) {
    e.preventDefault();
    const btn = document.getElementById('reg-btn');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner"></div>';

    try {
        const data = await api('/api/auth/register', {
            method: 'POST',
            body: JSON.stringify({
                display_name: document.getElementById('reg-name').value,
                username: document.getElementById('reg-username').value,
                email: document.getElementById('reg-email').value,
                password: document.getElementById('reg-password').value,
            }),
        });

        setToken(data.access_token);
        setUser(data.user);
        showToast('Account created! Welcome to Open IoT.', 'success');

        setTimeout(() => {
            window.location.href = '/dashboard';
        }, 500);
    } catch (err) {
        showToast(err.message || 'Registration failed', 'error');
        btn.disabled = false;
        btn.innerHTML = '<span>Create Account</span><span>→</span>';
    }
}
