#include <WiFi.h>
#include <WebServer.h>
#include <SimpleFTPServer.h>
#include <ESPmDNS.h>
#include "SD.h"
#include "SPI.h"
#include "FS.h"
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "TEST";
const char* password = "TEST";

// Access Point settings (used if WiFi connection fails)
const char* ap_ssid = "ESP32-FileServer";
const char* ap_password = "fileserver123";

// Web server port - standard HTTP port for better compatibility
const uint16_t webServerPort = 80;
WebServer webServer(webServerPort);

// FTP server instance
FtpServer ftpSrv;

// FTP credentials
const char* ftp_user = "esp32";
const char* ftp_password = "esp32";

// SD card configuration
#define SD_CS 5  // SD card chip select pin
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Global variables
bool wifiConnected = false;
bool accessPointMode = false;
String server_ip = "";

// Authentication variables
#define MAX_SESSIONS 5
#define SESSION_TIMEOUT 1800000  // 30 minutes in milliseconds
#define USERS_FILE "/users.json"
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASS "admin123"

// Session structure
struct Session {
  String token;
  String username;
  unsigned long lastActivity;
  bool isActive;
  String userLevel;  // "admin" or "user"
};

// Array to store active sessions
Session sessions[MAX_SESSIONS];

// File upload status variables
File uploadFile;
String uploadPath;

// Function declarations for authentication
bool authenticateUser(String username, String password, String &userLevel);
String generateSessionToken();
bool validateSession(String token, String &username, String &userLevel);
void updateSessionActivity(String token);
void createDefaultUsersFile();
void setupAuthentication();
bool removeDir(String path);
bool createDirRecursive(String path);
String urlDecode(String input);
String createSession(String username, String userLevel);

// Forward declarations for all handlers
void handleLogin();
void handleLogout();
void handleRoot();
void handleUserManagement();
void handleServerInfo();
void handleListFiles();
void handleDownload();
void handleDelete();
void handleCreateDir();
void handleFileUpload();
void handleUploadAuth();
void handleFileAccess();
void handleGetUsers();
void handleAddUser();
void handleUpdateUser();
void handleDeleteUser();

// HTML for login page
const char login_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Server - Login</title>
    <style>
        :root {
            --primary-color: #2c3e50;
            --secondary-color: #3498db;
            --accent-color: #e74c3c;
            --background-color: #ecf0f1;
            --text-color: #333;
            --light-text: #fff;
            --border-radius: 8px;
            --box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
            --transition: all 0.3s ease;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: var(--background-color);
            color: var(--text-color);
            line-height: 1.6;
            padding: 20px;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
        }
        
        .login-container {
            width: 100%;
            max-width: 400px;
            padding: 30px;
            background-color: #fff;
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
        }
        
        .login-header {
            text-align: center;
            margin-bottom: 25px;
        }
        
        .login-header h1 {
            color: var(--primary-color);
            font-size: 28px;
            margin-bottom: 10px;
        }
        
        .login-header p {
            color: #777;
            font-size: 14px;
        }
        
        .form-group {
            margin-bottom: 20px;
        }
        
        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 500;
            color: var(--primary-color);
        }
        
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 12px;
            border: 1px solid #ddd;
            border-radius: var(--border-radius);
            font-size: 16px;
            transition: var(--transition);
        }
        
        input[type="text"]:focus,
        input[type="password"]:focus {
            border-color: var(--secondary-color);
            outline: none;
            box-shadow: 0 0 0 2px rgba(52, 152, 219, 0.2);
        }
        
        .login-btn {
            width: 100%;
            background-color: var(--secondary-color);
            color: white;
            border: none;
            padding: 14px;
            border-radius: var(--border-radius);
            cursor: pointer;
            font-size: 16px;
            font-weight: 600;
            transition: var(--transition);
        }
        
        .login-btn:hover {
            background-color: #2980b9;
        }
        
        .error-message {
            color: var(--accent-color);
            font-size: 14px;
            margin-top: 20px;
            text-align: center;
        }
        
        .server-info {
            text-align: center;
            font-size: 12px;
            color: #777;
            margin-top: 30px;
        }
    </style>
</head>
<body>
    <div class="login-container">
        <div class="login-header">
            <h1>ESP32 File Server</h1>
            <p>Please log in to continue</p>
        </div>
        
        <form id="login-form" action="/login" method="post">
            <div class="form-group">
                <label for="username">Username</label>
                <input type="text" id="username" name="username" required>
            </div>
            
            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" required>
            </div>
            
            <button type="submit" class="login-btn">Log In</button>
        </form>
        
        <div class="error-message" id="error-message">
            %ERROR_MESSAGE%
        </div>
        
        <div class="server-info" id="server-info">
            %SERVER_INFO%
        </div>
    </div>
</body>
</html>
)rawliteral";

// HTML for user management page
const char user_management_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Server - User Management</title>
    <style>
        :root {
            --primary-color: #2c3e50;
            --secondary-color: #3498db;
            --accent-color: #e74c3c;
            --background-color: #ecf0f1;
            --text-color: #333;
            --light-text: #fff;
            --border-radius: 8px;
            --box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
            --transition: all 0.3s ease;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: var(--background-color);
            color: var(--text-color);
            line-height: 1.6;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background-color: #fff;
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
        }
        
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 20px;
            margin-bottom: 20px;
            border-bottom: 1px solid #ddd;
        }
        
        h1 {
            color: var(--primary-color);
            font-size: 24px;
        }
        
        .nav-links {
            display: flex;
            gap: 20px;
        }
        
        .nav-link {
            color: var(--secondary-color);
            text-decoration: none;
        }
        
        .nav-link:hover {
            text-decoration: underline;
        }
        
        .controls {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
        }
        
        .btn {
            background-color: var(--secondary-color);
            color: white;
            border: none;
            padding: 10px 15px;
            border-radius: var(--border-radius);
            cursor: pointer;
            font-size: 14px;
            display: flex;
            align-items: center;
            gap: 5px;
            transition: var(--transition);
            text-decoration: none;
        }
        
        .btn:hover {
            background-color: #2980b9;
            transform: translateY(-2px);
        }
        
        .btn-danger {
            background-color: var(--accent-color);
        }
        
        .btn-danger:hover {
            background-color: #c0392b;
        }
        
        .user-list {
            border: 1px solid #ddd;
            border-radius: var(--border-radius);
            overflow: hidden;
        }
        
        .list-header {
            display: grid;
            grid-template-columns: 1fr 1fr 100px;
            background-color: var(--primary-color);
            color: var(--light-text);
            padding: 10px 15px;
            font-weight: bold;
        }
        
        .user-item {
            display: grid;
            grid-template-columns: 1fr 1fr 100px;
            padding: 12px 15px;
            border-bottom: 1px solid #eee;
            align-items: center;
        }
        
        .user-item:last-child {
            border-bottom: none;
        }
        
        .user-actions {
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
        
        .user-action {
            cursor: pointer;
            color: var(--secondary-color);
            transition: var(--transition);
        }
        
        .user-action:hover {
            color: var(--accent-color);
        }
        
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.5);
            z-index: 1000;
            justify-content: center;
            align-items: center;
        }
        
        .modal-content {
            background-color: white;
            border-radius: var(--border-radius);
            width: 90%;
            max-width: 500px;
            box-shadow: var(--box-shadow);
            animation: modalFade 0.3s ease;
        }
        
        .modal-header {
            padding: 15px 20px;
            border-bottom: 1px solid #eee;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .modal-header h2 {
            font-size: 18px;
            color: var(--primary-color);
        }
        
        .close-modal {
            cursor: pointer;
            font-size: 22px;
            color: #777;
            transition: var(--transition);
        }
        
        .close-modal:hover {
            color: var(--accent-color);
        }
        
        .modal-body {
            padding: 20px;
        }
        
        .form-group {
            margin-bottom: 15px;
        }
        
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: 500;
        }
        
        input[type="text"],
        input[type="password"],
        select {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: var(--border-radius);
            font-size: 14px;
        }
        
        .modal-footer {
            padding: 15px 20px;
            border-top: 1px solid #eee;
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
        
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background-color: var(--primary-color);
            color: white;
            padding: 15px 20px;
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            z-index: 2000;
            opacity: 0;
            transition: opacity 0.3s ease;
            max-width: 300px;
        }
        
        .toast.success {
            background-color: #2ecc71;
        }
        
        .toast.error {
            background-color: var(--accent-color);
        }
        
        @keyframes modalFade {
            from {
                opacity: 0;
                transform: translateY(-20px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div>
                <h1>User Management</h1>
            </div>
            <div class="nav-links">
                <a href="#" onclick="navigateWithToken('/')" class="nav-link">File Manager</a>
                <a href="/logout" class="nav-link">Logout</a>
            </div>
        </header>
        
        <div class="controls">
            <button class="btn" id="add-user-btn">
                <span>👤</span> Add New User
            </button>
        </div>
        
        <div class="user-list">
            <div class="list-header">
                <div>Username</div>
                <div>User Level</div>
                <div>Actions</div>
            </div>
            <div id="user-container">
                <!-- User list will be populated here -->
            </div>
        </div>
    </div>
    
    <!-- Add/Edit User Modal -->
    <div class="modal" id="user-modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2 id="modal-title">Add New User</h2>
                <span class="close-modal">&times;</span>
            </div>
            <div class="modal-body">
                <form id="user-form" onsubmit="return false;">
                    <div class="form-group">
                        <label for="username-input">Username</label>
                        <input type="text" id="username-input" required>
                    </div>
                    <div class="form-group">
                        <label for="password-input">Password</label>
                        <input type="password" id="password-input" required>
                    </div>
                    <div class="form-group">
                        <label for="userlevel-input">User Level</label>
                        <select id="userlevel-input">
                            <option value="user">Regular User</option>
                            <option value="admin">Administrator</option>
                        </select>
                    </div>
                    <input type="hidden" id="edit-mode" value="add">
                    <input type="hidden" id="original-username" value="">
                </form>
            </div>
            <div class="modal-footer">
                <button type="button" class="btn" id="user-cancel">Cancel</button>
                <button type="button" class="btn btn-primary" id="user-submit">Save</button>
            </div>
        </div>
    </div>
    
    <!-- Confirmation Modal -->
    <div class="modal" id="confirm-modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Confirm Action</h2>
                <span class="close-modal">&times;</span>
            </div>
            <div class="modal-body">
                <p id="confirm-message">Are you sure you want to proceed?</p>
            </div>
            <div class="modal-footer">
                <button class="btn" id="confirm-cancel">Cancel</button>
                <button class="btn btn-danger" id="confirm-ok">OK</button>
            </div>
        </div>
    </div>
    
    <!-- Toast Notification -->
    <div class="toast" id="toast"></div>
    
    <script>
        // Navigation helper function
        function navigateWithToken(url) {
            const token = getSessionToken();
            if (token) {
                window.location.href = `${url}?token=${token}`;
            } else {
                window.location.href = url;
            }
        }
        
        // Global variables for DOM elements
        let userContainer, addUserBtn, userModal, confirmModal, modalTitle, userForm;
        let usernameInput, passwordInput, userlevelInput, editMode, originalUsername;
        let userSubmit, userCancel, confirmMessage, confirmOk, confirmCancel, toast;
        
        // Initialize DOM elements after page load
        document.addEventListener('DOMContentLoaded', () => {
            userContainer = document.getElementById('user-container');
            addUserBtn = document.getElementById('add-user-btn');
            userModal = document.getElementById('user-modal');
            confirmModal = document.getElementById('confirm-modal');
            modalTitle = document.getElementById('modal-title');
            userForm = document.getElementById('user-form');
            usernameInput = document.getElementById('username-input');
            passwordInput = document.getElementById('password-input');
            userlevelInput = document.getElementById('userlevel-input');
            editMode = document.getElementById('edit-mode');
            originalUsername = document.getElementById('original-username');
            userSubmit = document.getElementById('user-submit');
            userCancel = document.getElementById('user-cancel');
            confirmMessage = document.getElementById('confirm-message');
            confirmOk = document.getElementById('confirm-ok');
            confirmCancel = document.getElementById('confirm-cancel');
            toast = document.getElementById('toast');
            
            // Set up event listeners after elements are found
            setupEventListeners();
            // Load initial user list
            loadUsers();
        });
        
        function setupEventListeners() {
            // Add User button click
            addUserBtn.addEventListener('click', () => {
                console.log('Add User button clicked');
                resetUserForm();
                modalTitle.textContent = 'Add New User';
                editMode.value = 'add';
                openModal(userModal);
                // Clear any previous values
                usernameInput.value = '';
                passwordInput.value = '';
                userlevelInput.value = 'user';
            });
            
            // Modal handlers
            document.querySelectorAll('.close-modal').forEach(closeBtn => {
                closeBtn.addEventListener('click', () => closeAllModals());
            });
            
            userCancel.addEventListener('click', () => closeAllModals());
            confirmCancel.addEventListener('click', () => closeAllModals());
            
            // Form submission
            userForm.addEventListener('submit', (e) => {
                e.preventDefault();
                console.log('Form submitted');
                handleUserSubmit();
            });
            
            userSubmit.addEventListener('click', (e) => {
                e.preventDefault();
                console.log('Submit button clicked');
                handleUserSubmit();
            });
            
            // Close modals when clicking outside
            window.addEventListener('click', (e) => {
                if (e.target === userModal || e.target === confirmModal) {
                    closeAllModals();
                }
            });
        }
        
        function loadUsers() {
            const token = getSessionToken();
            fetch('/api/users', {
                headers: {
                    'Authorization': 'Bearer ' + token
                }
            })
                .then(response => {
                    if (!response.ok) {
                        throw new Error(`HTTP error! Status: ${response.status}`);
                    }
                    return response.json();
                })
                .then(users => {
                    displayUsers(users);
                })
                .catch(error => {
                    console.error('Error loading users:', error);
                    showToast('Failed to load users', 'error');
                });
        }
        
        function displayUsers(users) {
            if (users.length === 0) {
                userContainer.innerHTML = '<div style="padding: 20px; text-align: center;">No users found</div>';
                return;
            }
            
            let html = '';
            
            users.forEach(user => {
                html += `
                    <div class="user-item" data-username="${user.username}">
                        <div>${user.username}</div>
                        <div>${user.userLevel === 'admin' ? 'Administrator' : 'Regular User'}</div>
                        <div class="user-actions">
                            <span class="user-action" onclick="editUser('${user.username}', '${user.userLevel}')">✏️</span>
                            <span class="user-action" onclick="deleteUser('${user.username}')">🗑️</span>
                        </div>
                    </div>
                `;
            });
            
            userContainer.innerHTML = html;
        }
        
        function resetUserForm() {
            userForm.reset();
            usernameInput.disabled = false;
            passwordInput.placeholder = '';
        }
        
        function editUser(username, userLevel) {
            resetUserForm();
            modalTitle.textContent = 'Edit User';
            usernameInput.value = username;
            usernameInput.disabled = true; // Don't allow changing username
            userlevelInput.value = userLevel;
            passwordInput.placeholder = '(leave blank to keep unchanged)';
            editMode.value = 'edit';
            originalUsername.value = username;
            openModal(userModal);
        }
        
        function deleteUser(username) {
            confirmMessage.textContent = `Are you sure you want to delete user "${username}"?`;
            openModal(confirmModal);
            
            confirmOk.onclick = () => {
                const token = getSessionToken();
                fetch(`/api/users/${encodeURIComponent(username)}`, {
                    method: 'DELETE',
                    headers: {
                        'Authorization': 'Bearer ' + token
                    }
                })
                    .then(response => {
                        if (!response.ok) throw new Error('Failed to delete user');
                        return response.text();
                    })
                    .then(() => {
                        showToast('User deleted successfully', 'success');
                        closeAllModals();
                        loadUsers(); // Reload user list
                    })
                    .catch(error => {
                        console.error('Error deleting user:', error);
                        showToast('Failed to delete user', 'error');
                    });
            };
        }
        
        function handleUserSubmit() {
            try {
                console.log('Handling user submit');
                const username = usernameInput.value.trim();
                const password = passwordInput.value.trim();
                const userLevel = userlevelInput.value;
                const mode = editMode.value;
                const token = getSessionToken();
                
                console.log('Form values:', {
                    username,
                    password: password ? '(provided)' : '(empty)',
                    userLevel,
                    mode
                });
                
                if (!token) {
                    console.error('No authentication token found');
                    showToast('Authentication error - please try logging in again', 'error');
                    return;
                }
                
                if (!username) {
                    showToast('Username is required', 'error');
                    return;
                }
                
                if (mode === 'add' && !password) {
                    showToast('Password is required', 'error');
                    return;
                }
                
                console.log('Submitting user form:', { mode, username, userLevel });
                
                const url = mode === 'add' ? '/api/users' : `/api/users/${encodeURIComponent(originalUsername.value)}`;
                const method = mode === 'add' ? 'POST' : 'PUT';
                
                const body = {
                    username: username,
                    userLevel: userLevel
                };
                
                // Only include password if provided (required for new users)
                if (password || mode === 'add') {
                    body.password = password;
                }
                
                // Create JSON string and log it
                const jsonBody = JSON.stringify(body);
                console.log('Request payload:', jsonBody);
                
                fetch(url, {
                    method: method,
                    headers: {
                        'Content-Type': 'application/json',
                        'Authorization': 'Bearer ' + token,
                        'Accept': 'application/json, text/plain, */*'
                    },
                    body: jsonBody
                })
                .then(response => {
                    console.log('Response status:', response.status);
                    console.log('Response headers:', response.headers);
                    
                    // Always try to read response text for debugging
                    return response.text().then(text => {
                        console.log('Response text:', text);
                        if (!response.ok) {
                            if (response.status === 401) {
                                window.location.href = '/login?redirect=/users';
                                throw new Error('Unauthorized');
                            }
                            throw new Error(text || `Failed to ${mode} user`);
                        }
                        return text;
                    });
                })
                    .then(() => {
                        showToast(`User ${mode === 'add' ? 'added' : 'updated'} successfully`, 'success');
                        closeAllModals();
                        loadUsers(); // Reload user list
                    })
                    .catch(error => {
                        console.error('Error saving user:', error);
                        showToast(`Failed to ${mode === 'add' ? 'add' : 'update'} user`, 'error');
                    });
            } catch (error) {
                console.error('Error in handleUserSubmit:', error);
                showToast('An unexpected error occurred', 'error');
            }
        }
        
        function getSessionToken() {
            // First try to get token from URL
            const urlParams = new URLSearchParams(window.location.search);
            const tokenFromUrl = urlParams.get('token');
            if (tokenFromUrl) {
                // Store token in cookie
                document.cookie = "session_token=" + tokenFromUrl + "; path=/; max-age=1800; SameSite=Strict";
                return tokenFromUrl;
            }
            
            // If no token in URL, try cookie
            const cookies = document.cookie.split(';');
            for (let cookie of cookies) {
                const [name, value] = cookie.trim().split('=');
                if (name === 'session_token') {
                    return value;
                }
            }
            return '';
        }
        
        function openModal(modal) {
            closeAllModals(); // Close any open modals first
            modal.style.display = 'flex';
        }
        
        function closeAllModals() {
            document.querySelectorAll('.modal').forEach(modal => {
                modal.style.display = 'none';
            });
        }
        
        function showToast(message, type = 'info') {
            toast.textContent = message;
            toast.className = 'toast';
            toast.classList.add(type);
            toast.style.opacity = '1';
            
            setTimeout(() => {
                toast.style.opacity = '0';
            }, 3000);
        }
    </script>
</body>
</html>
)rawliteral";

// HTML, CSS, and JavaScript for the web interface (embedded directly in the code)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Server</title>
    <style>
        :root {
            --primary-color: #2c3e50;
            --secondary-color: #3498db;
            --accent-color: #e74c3c;
            --background-color: #ecf0f1;
            --text-color: #333;
            --light-text: #fff;
            --border-radius: 8px;
            --box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
            --transition: all 0.3s ease;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: var(--background-color);
            color: var(--text-color);
            line-height: 1.6;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background-color: #fff;
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
        }
        
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 20px;
            margin-bottom: 20px;
            border-bottom: 1px solid #ddd;
        }
        
        h1 {
            color: var(--primary-color);
            font-size: 24px;
        }
        
        .server-info {
            font-size: 14px;
            color: #777;
        }
        
        .nav-links {
            display: flex;
            gap: 20px;
            align-items: center;
        }
        
        .nav-link {
            color: var(--secondary-color);
            text-decoration: none;
        }
        
        .nav-link:hover {
            text-decoration: underline;
        }
        
        .user-info {
            font-size: 14px;
            color: #777;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        .user-info span {
            color: var(--secondary-color);
            font-weight: 500;
        }
        
        .controls {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            flex-wrap: wrap;
        }
        
        .btn {
            background-color: var(--secondary-color);
            color: white;
            border: none;
            padding: 10px 15px;
            border-radius: var(--border-radius);
            cursor: pointer;
            font-size: 14px;
            display: flex;
            align-items: center;
            gap: 5px;
            transition: var(--transition);
        }
        
        .btn:hover {
            background-color: #2980b9;
            transform: translateY(-2px);
        }
        
        .btn-danger {
            background-color: var(--accent-color);
        }
        
        .btn-danger:hover {
            background-color: #c0392b;
        }
        
        .path-navigator {
            display: flex;
            align-items: center;
            padding: 10px 15px;
            background-color: #f8f9fa;
            border-radius: var(--border-radius);
            margin-bottom: 20px;
            overflow-x: auto;
            white-space: nowrap;
        }
        
        .path-part {
            cursor: pointer;
            color: var(--secondary-color);
            margin-right: 5px;
            transition: var(--transition);
        }
        
        .path-part:hover {
            text-decoration: underline;
        }
        
        .separator {
            margin: 0 5px;
            color: #888;
        }
        
        .file-list {
            border: 1px solid #ddd;
            border-radius: var(--border-radius);
            overflow: hidden;
        }
        
        .file-list-header {
            display: grid;
            grid-template-columns: 30px 1fr 120px 100px;
            background-color: var(--primary-color);
            color: var(--light-text);
            padding: 10px 15px;
            font-weight: bold;
        }
        
        .file-item {
            display: grid;
            grid-template-columns: 30px 1fr 120px 100px;
            padding: 12px 15px;
            border-bottom: 1px solid #eee;
            align-items: center;
            cursor: pointer;
            transition: var(--transition);
        }
        
        .file-item:last-child {
            border-bottom: none;
        }
        
        .file-item:hover {
            background-color: #f8f9fa;
        }
        
        .file-icon {
            display: flex;
            justify-content: center;
            align-items: center;
            font-size: 18px;
            color: var(--secondary-color);
        }
        
        .file-name {
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        
        .file-size {
            text-align: right;
            color: #777;
        }
        
        .file-actions {
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
        
        .file-action {
            cursor: pointer;
            color: var(--secondary-color);
            transition: var(--transition);
        }
        
        .file-action:hover {
            color: var(--accent-color);
        }
        
        .modal {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.5);
            z-index: 1000;
            justify-content: center;
            align-items: center;
        }
        
        .modal-content {
            background-color: white;
            border-radius: var(--border-radius);
            width: 90%;
            max-width: 500px;
            box-shadow: var(--box-shadow);
            animation: modalFade 0.3s ease;
        }
        
        .modal-header {
            padding: 15px 20px;
            border-bottom: 1px solid #eee;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .modal-header h2 {
            font-size: 18px;
            color: var(--primary-color);
        }
        
        .close-modal {
            cursor: pointer;
            font-size: 22px;
            color: #777;
            transition: var(--transition);
        }
        
        .close-modal:hover {
            color: var(--accent-color);
        }
        
        .modal-body {
            padding: 20px;
        }
        
        .form-group {
            margin-bottom: 15px;
        }
        
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: 500;
        }
        
        input[type="text"],
        input[type="file"] {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: var(--border-radius);
            font-size: 14px;
        }
        
        .modal-footer {
            padding: 15px 20px;
            border-top: 1px solid #eee;
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
        
        .progress-bar-container {
            width: 100%;
            height: 20px;
            background-color: #eee;
            border-radius: var(--border-radius);
            margin-top: 15px;
            overflow: hidden;
            display: none;
        }
        
        .progress-bar {
            height: 100%;
            background-color: var(--secondary-color);
            width: 0%;
            transition: width 0.3s ease;
        }
        
        .empty-message {
            text-align: center;
            padding: 30px;
            color: #777;
        }
        
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background-color: var(--primary-color);
            color: white;
            padding: 15px 20px;
            border-radius: var(--border-radius);
            box-shadow: var(--box-shadow);
            z-index: 2000;
            opacity: 0;
            transition: opacity 0.3s ease;
            max-width: 300px;
        }
        
        .toast.success {
            background-color: #2ecc71;
        }
        
        .toast.error {
            background-color: var(--accent-color);
        }
        
        @keyframes modalFade {
            from {
                opacity: 0;
                transform: translateY(-20px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        
        @media (max-width: 768px) {
            .file-list-header,
            .file-item {
                grid-template-columns: 30px 1fr 80px;
            }
            
            .file-actions {
                display: none;
            }
            
            .mobile-actions {
                display: flex;
                justify-content: flex-end;
                gap: 15px;
                margin-top: 20px;
            }
            
            .controls {
                justify-content: center;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div>
                <h1>ESP32 File Server</h1>
                <p class="server-info" id="server-info">Loading...</p>
            </div>
            <div class="nav-links">
                <div class="user-info">
                    Logged in as: <span id="username-display">%USERNAME%</span>
                </div>
                <a href="#" onclick="navigateWithToken('/users')" class="nav-link admin-only">User Management</a>
                <a href="/logout" class="nav-link">Logout</a>
            </div>
        </header>
        
        <div class="controls">
            <button class="btn" id="upload-btn">
                <span>⬆️</span> Upload File
            </button>
            <button class="btn" id="new-folder-btn">
                <span>📁</span> New Folder
            </button>
            <button class="btn btn-danger" id="selected-delete-btn" style="display: none;">
                <span>🗑️</span> Delete Selected
            </button>
        </div>
        
        <div class="path-navigator" id="path-nav">
            <span class="path-part" data-path="/">Root</span>
        </div>
        
        <div class="file-list">
            <div class="file-list-header">
                <div></div>
                <div>Name</div>
                <div>Size</div>
                <div>Actions</div>
            </div>
            <div id="file-container">
                <div class="empty-message">Loading files...</div>
            </div>
        </div>
    </div>
    
    <!-- Upload File Modal -->
    <div class="modal" id="upload-modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Upload File</h2>
                <span class="close-modal">&times;</span>
            </div>
            <div class="modal-body">
                <form id="upload-form" enctype="multipart/form-data" method="post" action="/upload">
                    <div class="form-group">
                        <label for="file-input">Select File</label>
                        <input type="file" id="file-input" name="file" required>
                    </div>
                    <input type="hidden" id="upload-path" name="path" value="/">
                    <div class="progress-bar-container" id="upload-progress-container">
                        <div class="progress-bar" id="upload-progress"></div>
                    </div>
                </form>
            </div>
            <div class="modal-footer">
                <button class="btn" id="upload-cancel">Cancel</button>
                <button class="btn" id="upload-submit">Upload</button>
            </div>
        </div>
    </div>
    
    <!-- New Folder Modal -->
    <div class="modal" id="folder-modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Create New Folder</h2>
                <span class="close-modal">&times;</span>
            </div>
            <div class="modal-body">
                <form id="folder-form">
                    <div class="form-group">
                        <label for="folder-name">Folder Name</label>
                        <input type="text" id="folder-name" required placeholder="Enter folder name">
                    </div>
                </form>
            </div>
            <div class="modal-footer">
                <button class="btn" id="folder-cancel">Cancel</button>
                <button class="btn" id="folder-submit">Create</button>
            </div>
        </div>
    </div>
    
    <!-- Confirmation Modal -->
    <div class="modal" id="confirm-modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Confirm Action</h2>
                <span class="close-modal">&times;</span>
            </div>
            <div class="modal-body">
                <p id="confirm-message">Are you sure you want to proceed?</p>
            </div>
            <div class="modal-footer">
                <button class="btn" id="confirm-cancel">Cancel</button>
                <button class="btn btn-danger" id="confirm-ok">OK</button>
            </div>
        </div>
    </div>
    
    <!-- Toast Notification -->
    <div class="toast" id="toast"></div>
    
    <script>
        // Global variables
        let currentPath = '/';
        let selectedFiles = [];
        let currentUser = '%USERNAME%';
        let userLevel = '%USERLEVEL%';
        
        // DOM elements
        const fileContainer = document.getElementById('file-container');
        const pathNav = document.getElementById('path-nav');
        const serverInfo = document.getElementById('server-info');
        const uploadModal = document.getElementById('upload-modal');
        const folderModal = document.getElementById('folder-modal');
        const confirmModal = document.getElementById('confirm-modal');
        const uploadBtn = document.getElementById('upload-btn');
        const newFolderBtn = document.getElementById('new-folder-btn');
        const selectedDeleteBtn = document.getElementById('selected-delete-btn');
        const uploadForm = document.getElementById('upload-form');
        const uploadSubmit = document.getElementById('upload-submit');
        const uploadCancel = document.getElementById('upload-cancel');
        const uploadPath = document.getElementById('upload-path');
        const folderForm = document.getElementById('folder-form');
        const folderSubmit = document.getElementById('folder-submit');
        const folderCancel = document.getElementById('folder-cancel');
        const folderName = document.getElementById('folder-name');
        const confirmMessage = document.getElementById('confirm-message');
        const confirmOk = document.getElementById('confirm-ok');
        const confirmCancel = document.getElementById('confirm-cancel');
        const uploadProgressContainer = document.getElementById('upload-progress-container');
        const uploadProgress = document.getElementById('upload-progress');
        const toast = document.getElementById('toast');
        
        // Initialize the application
        document.addEventListener('DOMContentLoaded', () => {
            // Set server info
            fetch('/server-info')
                .then(response => response.json())
                .then(data => {
                    const { ip, port, ftp_user, ftp_password, mode, hostname } = data;
                    let infoText = `IP: ${ip}:${port} | Hostname: ${hostname} | FTP: ${ftp_user}:${ftp_password} | Mode: ${mode}`;
                    serverInfo.textContent = infoText;
                })
                .catch(error => {
                    console.error('Error fetching server info:', error);
                    serverInfo.textContent = 'Server info unavailable';
                });
            
            // Show/hide admin features
            if (userLevel === 'admin') {
                document.querySelectorAll('.admin-only').forEach(el => {
                    el.style.display = 'block';
                });
            } else {
                document.querySelectorAll('.admin-only').forEach(el => {
                    el.style.display = 'none';
                });
            }
            
            // Load initial files
            loadFiles(currentPath);
            
            // Setup event listeners
            setupEventListeners();
        });
        
        function setupEventListeners() {
            // Button click handlers
            uploadBtn.addEventListener('click', () => {
                uploadPath.value = currentPath;
                openModal(uploadModal);
            });
            newFolderBtn.addEventListener('click', () => openModal(folderModal));
            selectedDeleteBtn.addEventListener('click', handleBulkDelete);
            
            // Modal handlers
            document.querySelectorAll('.close-modal').forEach(closeBtn => {
                closeBtn.addEventListener('click', () => closeAllModals());
            });
            
            // Upload form handling
            uploadSubmit.addEventListener('click', () => {
                const fileInput = document.getElementById('file-input');
                if (fileInput.files.length === 0) {
                    showToast('Please select a file', 'error');
                    return;
                }

                // First get an upload token
                fetch('/upload-auth', {
                    headers: {
                        'Authorization': 'Bearer ' + getSessionToken()
                    }
                })
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Authentication failed');
                    }
                    return response.json();
                })
                .then(data => {
                    if (!data.token) {
                        throw new Error('No upload token received');
                    }
                    
                    // Create FormData
                    const formData = new FormData();
                    formData.append('file', fileInput.files[0]);
                    
                    // Show progress bar
                    uploadProgressContainer.style.display = 'block';
                    uploadProgress.style.width = '0%';
                    
                    // Use XMLHttpRequest for upload with progress
                    const xhr = new XMLHttpRequest();
                    
                    // Include token in URL and set it in headers
                    const uploadUrl = `/upload?path=${encodeURIComponent(currentPath)}&token=${encodeURIComponent(data.token)}`;
                    console.log('Starting upload with token:', data.token);
                    
                    xhr.open('POST', uploadUrl, true);
                    
                    // Set both headers for maximum compatibility
                    xhr.setRequestHeader('Authorization', 'Bearer ' + data.token);
                    xhr.setRequestHeader('X-Upload-Token', data.token);
                    
                    xhr.upload.onprogress = (e) => {
                        if (e.lengthComputable) {
                            const percentComplete = (e.loaded / e.total) * 100;
                            uploadProgress.style.width = percentComplete + '%';
                            console.log(`Upload progress: ${percentComplete}%`);
                        }
                    };
                    
                    xhr.onload = function() {
                        console.log('Upload completed with status:', xhr.status);
                        if (xhr.status === 200) {
                            showToast('File uploaded successfully', 'success');
                            // Reset the file input
                            fileInput.value = '';
                            // Reset and hide the progress bar
                            uploadProgress.style.width = '0%';
                            uploadProgressContainer.style.display = 'none';
                            // Close the modal
                            closeAllModals();
                            // Reload the file list with a longer delay
                            setTimeout(() => {
                                loadFiles(currentPath);
                            }, 2000); // Increased delay to ensure server has processed the file
                        } else {
                            console.error('Upload failed with status:', xhr.status);
                            showToast('Upload failed: ' + xhr.statusText, 'error');
                            uploadProgress.style.width = '0%';
                            uploadProgressContainer.style.display = 'none';
                        }
                    };
                    
                    xhr.onerror = function(e) {
                        console.error('Upload error:', e);
                        showToast('Upload failed', 'error');
                        uploadProgress.style.width = '0%';
                        uploadProgressContainer.style.display = 'none';
                    };
                    
                    console.log('Starting upload to:', uploadUrl);
                    xhr.send(formData);
                })
                .catch(error => {
                    console.error('Error:', error);
                    showToast('Authentication failed. Please try again.', 'error');
                    uploadProgress.style.width = '0%';
                    uploadProgressContainer.style.display = 'none';
                });
            });
            
            uploadCancel.addEventListener('click', () => closeAllModals());
            
            // Folder creation
            folderForm.addEventListener('submit', (e) => {
                e.preventDefault();
                handleFolderCreate();
            });
            
            folderSubmit.addEventListener('click', () => folderForm.dispatchEvent(new Event('submit')));
            folderCancel.addEventListener('click', () => closeAllModals());
            
            confirmCancel.addEventListener('click', () => closeAllModals());
            
            // Close modals when clicking outside
            window.addEventListener('click', (e) => {
                if (e.target === uploadModal || e.target === folderModal || e.target === confirmModal) {
                    closeAllModals();
                }
            });
        }
        
        function loadFiles(path) {
            // Clear selection when loading new directory
            selectedFiles = [];
            updateSelectedFilesUI();
            
            fetch(`/list?path=${encodeURIComponent(path)}`, {
                headers: {
                    'Authorization': 'Bearer ' + getSessionToken()
                }
            })
                .then(response => {
                    if (response.status === 401) {
                        // Unauthorized, redirect to login
                        window.location.href = '/login';
                        throw new Error('Unauthorized');
                    }
                    if (!response.ok) {
                        throw new Error(`HTTP error! Status: ${response.status}`);
                    }
                    return response.json();
                })
                .then(files => {
                    displayFiles(files, path);
                    updatePathNavigator(path);
                    currentPath = path;
                })
                .catch(error => {
                    console.error('Error loading files:', error);
                    showToast('Failed to load files', 'error');
                    fileContainer.innerHTML = `
                        <div class="empty-message">
                            Error loading files. <a href="#" onclick="loadFiles('/')">Go to root</a>
                        </div>
                    `;
                });
        }
        
        function getSessionToken() {
            // First try to get token from URL
            const urlParams = new URLSearchParams(window.location.search);
            const tokenFromUrl = urlParams.get('token');
            if (tokenFromUrl) {
                // Store token in cookie
                document.cookie = "session_token=" + tokenFromUrl + "; path=/; max-age=1800; SameSite=Strict";
                // Remove token from URL without reloading page
                const newUrl = window.location.pathname;
                window.history.replaceState({}, document.title, newUrl);
                return tokenFromUrl;
            }
            
            // If no token in URL, try cookie
            const cookies = document.cookie.split(';');
            for (let cookie of cookies) {
                const [name, value] = cookie.trim().split('=');
                if (name === 'session_token') {
                    return value;
                }
            }
            return '';
        }

        function navigateWithToken(url) {
            const token = getSessionToken();
            if (token) {
                window.location.href = `${url}?token=${token}`;
            } else {
                window.location.href = url;
            }
        }
        
        function displayFiles(files, path) {
            if (files.length === 0) {
                fileContainer.innerHTML = '<div class="empty-message">This folder is empty</div>';
                return;
            }
            
            // Sort files: directories first, then files, alphabetically
            files.sort((a, b) => {
                if (a.type === b.type) {
                    return a.name.localeCompare(b.name);
                }
                return a.type === 'dir' ? -1 : 1;
            });
            
            let html = '';
            
            // Add parent directory entry if not at root
            if (path !== '/') {
                const parentPath = path.split('/').slice(0, -2).join('/') + '/';
                html += `
                    <div class="file-item" data-path="${parentPath}" onclick="loadFiles('${parentPath}')">
                        <div class="file-icon">📁</div>
                        <div class="file-name">..</div>
                        <div class="file-size"></div>
                        <div class="file-actions"></div>
                    </div>
                `;
            }
            
            // Add all files and directories
            files.forEach(file => {
                const isDir = file.type === 'dir';
                const icon = isDir ? '📁' : getFileIcon(file.name);
                
                html += `
                    <div class="file-item" data-path="${file.path}" data-type="${file.type}">
                        <div class="file-icon">${icon}</div>
                        <div class="file-name" onclick="${isDir ? `loadFiles('${file.path}')` : `previewFile('${file.path}')`}">${file.name}</div>
                        <div class="file-size">${file.size}</div>
                        <div class="file-actions">
                            ${isDir ? '' : `<span class="file-action" onclick="downloadFile('${file.path}', '${file.name}')">⬇️</span>`}
                            <span class="file-action" onclick="deleteItem('${file.path}', '${isDir ? '1' : '0'}', '${file.name}')">🗑️</span>
                        </div>
                    </div>
                `;
            });
            
            fileContainer.innerHTML = html;
            
            // Add selection capability
            document.querySelectorAll('.file-item').forEach(item => {
                if (!item.dataset.path || item.dataset.path === '../') return;
                
                item.addEventListener('contextmenu', (e) => {
                    e.preventDefault();
                    toggleFileSelection(item);
                });
            });
        }
        
        function getFileIcon(fileName) {
            const ext = fileName.split('.').pop().toLowerCase();
            
            const iconMap = {
                // Text and documents
                'txt': '📄',
                'pdf': '📑',
                'doc': '📝',
                'docx': '📝',
                'xls': '📊',
                'xlsx': '📊',
                'ppt': '📊',
                'pptx': '📊',
                
                // Media
                'jpg': '🖼️',
                'jpeg': '🖼️',
                'png': '🖼️',
                'gif': '🖼️',
                'bmp': '🖼️',
                'mp3': '🎵',
                'wav': '🎵',
                'mp4': '🎬',
                'mov': '🎬',
                'avi': '🎬',
                
                // Code and web
                'html': '🌐',
                'css': '🌐',
                'js': '🌐',
                'json': '🌐',
                'xml': '🌐',
                'php': '🌐',
                'py': '🐍',
                'c': '📟',
                'cpp': '📟',
                'h': '📟',
                'java': '📟',
                
                // Archives
                'zip': '📦',
                'rar': '📦',
                '7z': '📦',
                'tar': '📦',
                'gz': '📦',
                
                // Others
                'md': '📑',
                'csv': '📊'
            };
            
            return iconMap[ext] || '📄';
        }
        
        function updatePathNavigator(path) {
            // Split path into parts
            const parts = path.split('/').filter(part => part);
            
            let html = '<span class="path-part" data-path="/">Root</span>';
            let currentPathBuild = '/';
            
            parts.forEach((part, index) => {
                currentPathBuild += part + '/';
                html += `
                    <span class="separator">/</span>
                    <span class="path-part" data-path="${currentPathBuild}">${part}</span>
                `;
            });
            
            pathNav.innerHTML = html;
            
            // Add click event to path parts
            document.querySelectorAll('.path-part').forEach(part => {
                part.addEventListener('click', () => {
                    loadFiles(part.dataset.path);
                });
            });
        }
        
        function handleFolderCreate() {
            const name = folderName.value.trim();
            
            if (!name) {
                showToast('Please enter a folder name', 'error');
                return;
            }
            
            const path = currentPath + name;
            
            fetch('/create-dir', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                    'Authorization': 'Bearer ' + getSessionToken()
                },
                body: `path=${encodeURIComponent(path)}`
            })
            .then(response => {
                if (response.status === 401) {
                    // Unauthorized, redirect to login
                    window.location.href = '/login';
                    throw new Error('Unauthorized');
                }
                if (!response.ok) throw new Error('Failed to create folder');
                return response.text();
            })
            .then(() => {
                showToast('Folder created successfully', 'success');
                closeAllModals();
                loadFiles(currentPath); // Reload file list
            })
            .catch(error => {
                console.error('Error creating folder:', error);
                showToast('Failed to create folder', 'error');
            });
        }
        
        function deleteItem(path, isDir, name) {
            confirmMessage.textContent = `Are you sure you want to delete ${name}?`;
            openModal(confirmModal);
            
            confirmOk.onclick = () => {
                fetch(`/delete?path=${encodeURIComponent(path)}&isDir=${isDir}`, {
                    method: 'DELETE',
                    headers: {
                        'Authorization': 'Bearer ' + getSessionToken()
                    }
                })
                .then(response => {
                    if (response.status === 401) {
                        // Unauthorized, redirect to login
                        window.location.href = '/login';
                        throw new Error('Unauthorized');
                    }
                    if (!response.ok) throw new Error('Failed to delete item');
                    return response.text();
                })
                .then(() => {
                    showToast('Item deleted successfully', 'success');
                    closeAllModals();
                    loadFiles(currentPath); // Reload file list
                })
                .catch(error => {
                    console.error('Error deleting item:', error);
                    showToast('Failed to delete item', 'error');
                });
            };
        }
        
        function downloadFile(path, name) {
            // Append auth token as query param
            window.location.href = `/download?path=${encodeURIComponent(path)}&token=${getSessionToken()}`;
        }
        
        function previewFile(path) {
            // Get file extension
            const ext = path.split('.').pop().toLowerCase();
            
            // Check if it's a previewable file type
            const previewableTypes = ['jpg', 'jpeg', 'png', 'gif', 'txt', 'pdf', 'html', 'htm'];
            
            if (previewableTypes.includes(ext)) {
                // Add auth token
                window.open(`${path}?token=${getSessionToken()}`, '_blank');
            } else {
                // For non-previewable files, offer download
                downloadFile(path, path.split('/').pop());
            }
        }
        
        function toggleFileSelection(fileItem) {
            const path = fileItem.dataset.path;
            
            if (selectedFiles.includes(path)) {
                // Remove from selection
                selectedFiles = selectedFiles.filter(f => f !== path);
                fileItem.style.backgroundColor = '';
            } else {
                // Add to selection
                selectedFiles.push(path);
                fileItem.style.backgroundColor = '#e3f2fd';
            }
            
            updateSelectedFilesUI();
        }
        
        function updateSelectedFilesUI() {
            if (selectedFiles.length > 0) {
                selectedDeleteBtn.style.display = 'flex';
                selectedDeleteBtn.textContent = `🗑️ Delete Selected (${selectedFiles.length})`;
            } else {
                selectedDeleteBtn.style.display = 'none';
            }
        }
        
        function handleBulkDelete() {
            if (selectedFiles.length === 0) return;
            
            confirmMessage.textContent = `Are you sure you want to delete ${selectedFiles.length} selected items?`;
            openModal(confirmModal);
            
            confirmOk.onclick = () => {
                // Process deletion sequentially
                let deletePromises = selectedFiles.map(path => {
                    const fileItem = document.querySelector(`[data-path="${path}"]`);
                    const isDir = fileItem ? fileItem.dataset.type === 'dir' : '0';
                    
                    return fetch(`/delete?path=${encodeURIComponent(path)}&isDir=${isDir}`, {
                        method: 'DELETE',
                        headers: {
                            'Authorization': 'Bearer ' + getSessionToken()
                        }
                    });
                });
                
                Promise.all(deletePromises)
                    .then(() => {
                        showToast('Selected items deleted successfully', 'success');
                        closeAllModals();
                        loadFiles(currentPath); // Reload file list
                    })
                    .catch(error => {
                        console.error('Error deleting items:', error);
                        showToast('Failed to delete some items', 'error');
                        closeAllModals();
                        loadFiles(currentPath); // Reload file list
                    });
            };
        }
        
        function openModal(modal) {
            closeAllModals(); // Close any open modals first
            modal.style.display = 'flex';
        }
        
        function closeAllModals() {
            // Reset forms
            if (uploadForm) uploadForm.reset();
            if (folderForm) folderForm.reset();
            
            // Hide progress bar
            uploadProgressContainer.style.display = 'none';
            uploadProgress.style.width = '0%';
            
            // Close all modals
            document.querySelectorAll('.modal').forEach(modal => {
                modal.style.display = 'none';
            });
        }
        
        function showToast(message, type = 'info') {
            toast.textContent = message;
            toast.className = 'toast';
            toast.classList.add(type);
            toast.style.opacity = '1';
            
            setTimeout(() => {
                toast.style.opacity = '0';
            }, 3000);
        }
    </script>
</body>
</html>
)rawliteral";

// Function to initialize SD card
bool initSDCard() {
  // Set custom SPI pins for SD card
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  
  if(!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed!");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD Card attached!");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if(cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  return true;
}

// Function to connect to WiFi with multiple attempts
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi...");
  
  // Wait for connection with extended timeout and multiple attempts
  int attempts = 0;
  const int maxAttempts = 3;
  
  while (attempts < maxAttempts) {
    unsigned long startTime = millis();
    
    // Try for 30 seconds per attempt
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      server_ip = WiFi.localIP().toString();
      wifiConnected = true;
      accessPointMode = false;
      
      // Start mDNS responder
      if (MDNS.begin("esp32files")) {
        Serial.println("mDNS responder started - You can access the server at http://esp32files.local");
        MDNS.addService("http", "tcp", webServerPort);
      } else {
        Serial.println("Error starting mDNS responder!");
      }
      
      return true;
    } else {
      attempts++;
      Serial.println("\nWiFi connection attempt " + String(attempts) + " failed.");
      
      if (attempts < maxAttempts) {
        Serial.println("Retrying...");
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(ssid, password);
        Serial.print("Connecting to WiFi...");
      }
    }
  }
  
  Serial.println("\nWiFi connection failed after " + String(maxAttempts) + " attempts. Starting Access Point...");
  setupAccessPoint();
  return false;
}

// Function to set up an Access Point if WiFi connection fails
void setupAccessPoint() {
  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  server_ip = WiFi.softAPIP().toString();
  Serial.print("Access Point Started. IP Address: ");
  Serial.println(server_ip);
  wifiConnected = false; // Not connected to external WiFi
  accessPointMode = true; // In AP mode
  
  // Start mDNS responder in AP mode too
  if (MDNS.begin("esp32files")) {
    Serial.println("mDNS responder started - You can access the server at http://esp32files.local");
    MDNS.addService("http", "tcp", webServerPort);
  } else {
    Serial.println("Error starting mDNS responder!");
  }
}

// Authentication related functions
void setupAuthentication() {
  // Check if users file exists, if not create with default admin
  if (!SD.exists(USERS_FILE)) {
    createDefaultUsersFile();
  }

  // Initialize sessions
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].isActive = false;
  }
}

// Create default users file with admin account
void createDefaultUsersFile() {
  // First remove old file if it exists
  if (SD.exists(USERS_FILE)) {
    SD.remove(USERS_FILE);
    Serial.println("Removed existing users file");
  }

  File file = SD.open(USERS_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create users file!");
    return;
  }

  // Create a simpler JSON structure
  String jsonStr = "{\"users\":[{\"username\":\"admin\",\"password\":\"admin123\",\"userLevel\":\"admin\"}]}";
  
  if (file.print(jsonStr)) {
    Serial.println("Default user file created successfully");
    Serial.println("JSON content: " + jsonStr);
  } else {
    Serial.println("Failed to write to users file!");
  }
  
  file.close();
  
  // Verify the file was written correctly
  file = SD.open(USERS_FILE);
  if (file) {
    String content = "";
    while (file.available()) {
      content += (char)file.read();
    }
    Serial.println("Read back users file content: " + content);
    file.close();
  } else {
    Serial.println("Failed to read back users file");
  }
}

// Generate a random session token
String generateSessionToken() {
  String token = "";
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  
  for (int i = 0; i < 32; i++) {
    int index = random(0, 62);
    token += charset[index];
  }
  
  return token;
}

// Create a new session for a user
String createSession(String username, String userLevel) {
  unsigned long currentTime = millis();
  String token = generateSessionToken();
  
  // Find an inactive session slot or the oldest active one
  int oldestIndex = 0;
  unsigned long oldestTime = currentTime;
  
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].isActive) {
      oldestIndex = i;
      break;
    }
    
    if (sessions[i].lastActivity < oldestTime) {
      oldestTime = sessions[i].lastActivity;
      oldestIndex = i;
    }
  }
  
  // Store new session
  sessions[oldestIndex].token = token;
  sessions[oldestIndex].username = username;
  sessions[oldestIndex].lastActivity = currentTime;
  sessions[oldestIndex].isActive = true;
  sessions[oldestIndex].userLevel = userLevel;
  
  return token;
}

// Authenticate user based on username and password
bool authenticateUser(String username, String password, String &userLevel) {
  Serial.println("Authenticating user: " + username);
  
  if (!SD.exists(USERS_FILE)) {
    Serial.println("Users file not found, creating default");
    createDefaultUsersFile();
  }

  File file = SD.open(USERS_FILE);
  if (!file) {
    Serial.println("Failed to open users file for authentication");
    return false;
  }

  String fileContent = "";
  while (file.available()) {
    fileContent += (char)file.read();
  }
  file.close();
  
  Serial.println("File content: " + fileContent);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, fileContent);
  
  if (error) {
    Serial.println("Failed to parse users file: " + String(error.c_str()));
    return false;
  }

  JsonArray users = doc["users"];
  if (users.isNull()) {
    Serial.println("No users array found in JSON");
    return false;
  }
  
  Serial.println("Number of users found: " + String(users.size()));

  for (JsonObject user : users) {
    String storedUsername = user["username"] | "";
    String storedPassword = user["password"] | "";
    
    Serial.println("Checking against: User=" + storedUsername + ", Pass=" + storedPassword);
    
    if (storedUsername.equals(username) && storedPassword.equals(password)) {
      userLevel = user["userLevel"] | "user";
      Serial.println("Authentication successful! User level: " + userLevel);
      return true;
    }
  }

  Serial.println("Authentication failed");
  return false;
}

// Validate a session token and return the associated username
bool validateSession(String token, String &username, String &userLevel) {
  unsigned long currentTime = millis();
  
  Serial.println("Validating session token: " + token);
  
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == token) {
      // Check if session has expired
      if (currentTime - sessions[i].lastActivity > SESSION_TIMEOUT) {
        Serial.println("Session expired");
        sessions[i].isActive = false;
        return false;
      }
      
      // Update last activity time
      sessions[i].lastActivity = currentTime;
      username = sessions[i].username;
      userLevel = sessions[i].userLevel;
      Serial.println("Session valid for user: " + username + ", level: " + userLevel);
      return true;
    }
  }
  
  Serial.println("No matching session found for token");
  return false;
}

// Update the last activity time for a session
void updateSessionActivity(String token) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == token) {
      sessions[i].lastActivity = millis();
      return;
    }
  }
}

// Check if a request is authenticated
bool isAuthenticated(WebServer &server, String &username, String &userLevel) {
  Serial.println("Checking authentication...");
  String token = "";
  
  // First check Authorization header
  if (server.hasHeader("Authorization")) {
    String authHeader = server.header("Authorization");
    if (authHeader.startsWith("Bearer ")) {
      token = authHeader.substring(7);
      Serial.println("Found token in Authorization header: " + token);
    }
  }
  
  // Then check URL parameter if no token found yet
  if (token.isEmpty() && server.hasArg("token")) {
    token = server.arg("token");
    Serial.println("Found token in URL parameter: " + token);
  }
  
  // Finally check cookies if still no token
  if (token.isEmpty() && server.hasHeader("Cookie")) {
    String cookies = server.header("Cookie");
    Serial.println("Checking cookies: " + cookies);
    
    int tokenStartPos = cookies.indexOf("session_token=");
    if (tokenStartPos != -1) {
      tokenStartPos += 14; // Length of "session_token="
      int tokenEndPos = cookies.indexOf(";", tokenStartPos);
      if (tokenEndPos != -1) {
        token = cookies.substring(tokenStartPos, tokenEndPos);
      } else {
        token = cookies.substring(tokenStartPos);
      }
      Serial.println("Found token in cookie: " + token);
    }
  }
  
  // If we found a token, validate it
  if (!token.isEmpty()) {
    bool isValid = validateSession(token, username, userLevel);
    if (isValid) {
      // If valid, ensure the token is set in the response cookie
      server.sendHeader("Set-Cookie", "session_token=" + token + "; Path=/; Max-Age=1800");
      return true;
    }
  }
  
  Serial.println("Authentication failed - no valid token found");
  return false;
}

// Handler for login page
void handleLogin() {
  String errorMessage = "";
  bool loginFailed = false;
  
  if (webServer.method() == HTTP_POST) {
    String username = webServer.arg("username");
    String password = webServer.arg("password");
    String userLevel;
    
    Serial.println("Login attempt: Username=" + username);
    
    if (authenticateUser(username, password, userLevel)) {
      String token = createSession(username, userLevel);
      Serial.println("Authentication successful, generated token: " + token);
      
      // Set token in cookie AND redirect with token in URL
      webServer.sendHeader("Set-Cookie", "session_token=" + token + "; Path=/; Max-Age=1800");
      
      // Check if there's a redirect URL
      String redirectUrl = webServer.hasArg("redirect") ? webServer.arg("redirect") : "/";
      
              // Simple redirect with token in URL and cookie
      webServer.sendHeader("Set-Cookie", "session_token=" + token + "; Path=/; Max-Age=1800; SameSite=Strict");
      
      String finalRedirectUrl = redirectUrl;
      if (finalRedirectUrl.indexOf('?') != -1) {
        finalRedirectUrl += "&token=" + token;
      } else {
        finalRedirectUrl += "?token=" + token;
      }
      
              webServer.sendHeader("Location", finalRedirectUrl, true);
        webServer.send(302, "text/plain", "");
      Serial.println("Sent redirect HTML with token in URL");
      return;
    } else {
      errorMessage = "Invalid username or password";
      loginFailed = true;
      Serial.println("Authentication failed");
    }
  }
  
  String serverInfo = "";
  if (accessPointMode) {
    serverInfo = "Running in Access Point Mode | SSID: " + String(ap_ssid) + " | Password: " + String(ap_password);
  } else {
    serverInfo = "Connected to WiFi | IP: " + server_ip;
  }
  
  String html = String(login_html);
  html.replace("%ERROR_MESSAGE%", loginFailed ? errorMessage : "");
  html.replace("%SERVER_INFO%", serverInfo);
  
  webServer.send(200, "text/html", html);
}

// Handler for logout
void handleLogout() {
  String username, userLevel;
  if (isAuthenticated(webServer, username, userLevel)) {
    // Find and invalidate the session
    String token;
    if (webServer.hasHeader("Authorization")) {
      token = webServer.header("Authorization").substring(7);
    } else if (webServer.hasArg("token")) {
      token = webServer.arg("token");
    }
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].isActive && sessions[i].token == token) {
        sessions[i].isActive = false;
        break;
      }
    }
  }
  
  // Clear the cookie and redirect to login
  webServer.sendHeader("Set-Cookie", "session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  webServer.sendHeader("Location", "/login");
  webServer.send(302);
}

// Handle the root page (file manager)
void handleRoot() {
  String username, userLevel;
  
  Serial.println("\n--- Root page request ---");
  Serial.println("Request URI: " + webServer.uri());
  if (webServer.args() > 0) {
    Serial.println("Request has " + String(webServer.args()) + " arguments");
    for (int i = 0; i < webServer.args(); i++) {
      Serial.println(webServer.argName(i) + ": " + webServer.arg(i));
    }
  }
  
  if (webServer.hasHeader("Cookie")) {
    Serial.println("Cookies: " + webServer.header("Cookie"));
  } else {
    Serial.println("No cookies found");
  }
  
  if (!isAuthenticated(webServer, username, userLevel)) {
    Serial.println("Not authenticated, redirecting to login");
    webServer.sendHeader("Location", "/login");
    webServer.send(302);
    return;
  }
  
  Serial.println("User authenticated as: " + username + " with level: " + userLevel);
  String html = String(index_html);
  html.replace("%USERNAME%", username);
  html.replace("%USERLEVEL%", userLevel);
  
  webServer.send(200, "text/html", html);
  Serial.println("Sent file manager page");
}

// Handle user management page
void handleUserManagement() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.sendHeader("Location", "/login");
    webServer.send(302);
    return;
  }
  
  // Only admin users can access user management
  if (userLevel != "admin") {
    webServer.sendHeader("Location", "/");
    webServer.send(302);
    return;
  }
  
  webServer.send(200, "text/html", user_management_html);
}

// API endpoint to get server information
void handleServerInfo() {
  String mode = accessPointMode ? "Access Point" : "WiFi Client";
  String hostname = "esp32files.local";
  
  String response = "{";
  response += "\"ip\":\"" + server_ip + "\",";
  response += "\"port\":" + String(webServerPort) + ",";
  response += "\"ftp_user\":\"" + String(ftp_user) + "\",";
  response += "\"ftp_password\":\"" + String(ftp_password) + "\",";
  response += "\"mode\":\"" + mode + "\",";
  response += "\"hostname\":\"" + hostname + "\"";
  response += "}";
  
  webServer.send(200, "application/json", response);
}

// handle pre-authentication for uploads
void handleUploadAuth() {
  String username, userLevel;
  
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  // The session token is already validated at this point, so we can just return it
  String sessionToken = "";
  
  // Get the token from the request
  if (webServer.hasHeader("Authorization")) {
    sessionToken = webServer.header("Authorization").substring(7);
  } else if (webServer.hasArg("token")) {
    sessionToken = webServer.arg("token");
  } else if (webServer.hasHeader("Cookie")) {
    String cookies = webServer.header("Cookie");
    int tokenStartPos = cookies.indexOf("session_token=");
    if (tokenStartPos != -1) {
      tokenStartPos += 14;
      int tokenEndPos = cookies.indexOf(";", tokenStartPos);
      if (tokenEndPos != -1) {
        sessionToken = cookies.substring(tokenStartPos, tokenEndPos);
      } else {
        sessionToken = cookies.substring(tokenStartPos);
      }
    }
  }
  
  String response = "{\"token\":\"" + sessionToken + "\"}";
  webServer.send(200, "application/json", response);
  Serial.println("Upload authentication token provided: " + sessionToken);
}

// API endpoint to list files
void handleListFiles() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  if (path.indexOf("..") != -1) {
    webServer.send(400, "text/plain", "Invalid path");
    return;
  }
  
  // Ensure path ends with '/'
  if (path.length() > 1 && path[path.length()-1] != '/') {
    path += '/';
  }
  
  File root = SD.open(path);
  if (!root) {
    webServer.send(404, "text/plain", "Path not found");
    return;
  }
  
  if (!root.isDirectory()) {
    root.close();
    webServer.send(400, "text/plain", "Not a directory");
    return;
  }
  
  String output = "[";
  File file = root.openNextFile();
  
  bool isFirst = true;
  while (file) {
    if (!isFirst) output += ",";
    isFirst = false;
    
    output += "{";
    output += "\"name\":\"" + String(file.name()) + "\",";
    output += "\"type\":\"" + String(file.isDirectory() ? "dir" : "file") + "\",";
    output += "\"path\":\"" + path + String(file.name()) + (file.isDirectory() ? "/" : "") + "\",";
    
    if (file.isDirectory()) {
      output += "\"size\":\"-\"";
    } else {
      // Format file size
      int fileSize = file.size();
      String sizeStr;
      
      if (fileSize < 1024) {
        sizeStr = String(fileSize) + " B";
      } else if (fileSize < 1024 * 1024) {
        sizeStr = String(fileSize / 1024.0, 1) + " KB";
      } else {
        sizeStr = String(fileSize / (1024.0 * 1024.0), 1) + " MB";
      }
      
      output += "\"size\":\"" + sizeStr + "\"";
    }
    
    output += "}";
    file = root.openNextFile();
  }
  
  root.close();
  output += "]";
  
  webServer.send(200, "application/json", output);
}

// Handler for file downloading
void handleDownload() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  String path = webServer.arg("path");
  if (path.indexOf("..") != -1) {
    webServer.send(400, "text/plain", "Invalid path");
    return;
  }
  
  if (!SD.exists(path)) {
    webServer.send(404, "text/plain", "File not found");
    return;
  }
  
  File file = SD.open(path, FILE_READ);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open file");
    return;
  }
  
  if (file.isDirectory()) {
    file.close();
    webServer.send(400, "text/plain", "Cannot download a directory");
    return;
  }
  
  String fileName = path;
  int lastSlash = fileName.lastIndexOf('/');
  if (lastSlash >= 0) {
    fileName = fileName.substring(lastSlash + 1);
  }
  
  // Determine content type based on file extension
  String contentType = "application/octet-stream";
  if (path.endsWith(".html") || path.endsWith(".htm")) contentType = "text/html";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".json")) contentType = "application/json";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (path.endsWith(".gif")) contentType = "image/gif";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  else if (path.endsWith(".xml")) contentType = "text/xml";
  else if (path.endsWith(".pdf")) contentType = "application/pdf";
  else if (path.endsWith(".zip")) contentType = "application/zip";
  else if (path.endsWith(".txt")) contentType = "text/plain";
  
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
  webServer.sendHeader("Content-Type", contentType);
  webServer.sendHeader("Content-Length", String(file.size()));
  webServer.streamFile(file, contentType);
  
  file.close();
}

// Handler for file deletion
void handleDelete() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  String path = webServer.arg("path");
  bool isDir = webServer.arg("isDir") == "1";
  
  if (path.indexOf("..") != -1) {
    webServer.send(400, "text/plain", "Invalid path");
    return;
  }
  
  bool success = false;
  if (isDir) {
    success = removeDir(path);
  } else {
    success = SD.remove(path);
  }
  
  if (success) {
    webServer.send(200, "text/plain", "Deleted");
  } else {
    webServer.send(500, "text/plain", "Failed to delete");
  }
}

// Helper function to recursively remove a directory
bool removeDir(String path) {
  File root = SD.open(path);
  if (!root) {
    return false;
  }
  
  if (!root.isDirectory()) {
    root.close();
    return false;
  }
  
  File file = root.openNextFile();
  while (file) {
    String filePath = path + "/" + String(file.name());
    if (file.isDirectory()) {
      file.close();
      removeDir(filePath);
    } else {
      file.close();
      SD.remove(filePath);
    }
    file = root.openNextFile();
  }
  
  root.close();
  return SD.rmdir(path);
}

// Handler for directory creation
void handleCreateDir() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  String path = webServer.arg("path");
  if (path.indexOf("..") != -1) {
    webServer.send(400, "text/plain", "Invalid path");
    return;
  }
  
  // Create parent directories if they don't exist
  createDirRecursive(path);
  
  if (SD.exists(path)) {
    webServer.send(200, "text/plain", "Directory created");
  } else {
    webServer.send(500, "text/plain", "Failed to create directory");
  }
}

// Helper function to recursively create directories
bool createDirRecursive(String path) {
  if (SD.exists(path)) {
    return true;
  }
  
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = path.substring(0, lastSlash);
    if (!createDirRecursive(parentPath)) {
      return false;
    }
  }
  
  return SD.mkdir(path);
}

// Helper function to decode URL-encoded strings
String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  
  for (unsigned int i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      if (i + 2 < input.length()) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  
  return decoded;
}

// Handler for file uploads
void handleFileUpload() {
  HTTPUpload& upload = webServer.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("\n=== UPLOAD START ===");
    Serial.println("Filename: " + upload.filename);
    
    // Check for authentication token
    String token = "";
    
    // Check headers
    if (webServer.header("Authorization").startsWith("Bearer ")) {
      token = webServer.header("Authorization").substring(7);
    } else if (webServer.header("X-Upload-Token") != "") {
      token = webServer.header("X-Upload-Token");
    } 
    // Check URL params
    else if (webServer.hasArg("token")) {
      token = webServer.arg("token");
    }
    
    // Validate the token
    String username, userLevel;
    if (token.isEmpty() || !validateSession(token, username, userLevel)) {
      Serial.println("Upload authentication failed: Invalid or missing token");
      return;
    }
    
    // Get path from URL
    String path = "/";
    if (webServer.hasArg("path")) {
      path = urlDecode(webServer.arg("path"));
      if (path.indexOf("..") != -1) {
        Serial.println("Invalid path");
        return;
      }
      if (path.length() > 0 && path[path.length()-1] != '/') {
        path += '/';
      }
    }
    
    // Create full upload path
    uploadPath = path + upload.filename;
    Serial.println("Upload path: " + uploadPath);
    
    // Check if file exists and remove it
    if (SD.exists(uploadPath)) {
      if (!SD.remove(uploadPath)) {
        Serial.println("Failed to remove existing file");
        return;
      }
    }
    
    // Open file for writing
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to open file for writing");
      return;
    }
    
    Serial.println("File opened for writing");
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("Upload completed successfully");
      Serial.println("Wrote " + String(upload.totalSize) + " bytes");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("Upload aborted");
    if (uploadFile) {
      uploadFile.close();
      SD.remove(uploadPath);
    }
  }
}

// API handlers for user management
void handleGetUsers() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  // Only admin users can access user list
  if (userLevel != "admin") {
    webServer.send(403, "text/plain", "Forbidden");
    return;
  }
  
  if (!SD.exists(USERS_FILE)) {
    webServer.send(200, "application/json", "[]");
    return;
  }
  
  File file = SD.open(USERS_FILE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file");
    return;
  }
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    webServer.send(500, "text/plain", "Failed to read users file");
    return;
  }
  
  JsonArray users = doc["users"];
  
  // Create a new document with only username and userLevel (no passwords)
  StaticJsonDocument<1024> outputDoc;
  JsonArray outputUsers = outputDoc.createNestedArray();
  
  for (JsonObject user : users) {
    JsonObject outputUser = outputUsers.createNestedObject();
    outputUser["username"] = user["username"];
    outputUser["userLevel"] = user["userLevel"] | "user"; // Default to "user" if userLevel is not set
  }
  
  String output;
  serializeJson(outputUsers, output);
  webServer.send(200, "application/json", output);
}

void handleAddUser() {
  Serial.println("Handling add user request");
  
  // Log all headers for debugging
  Serial.println("\nRequest Headers:");
  for (int i = 0; i < webServer.headers(); i++) {
    Serial.print(webServer.headerName(i));
    Serial.print(": ");
    Serial.println(webServer.header(i));
  }

  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    Serial.println("Authentication failed");
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  // Only admin users can add users
  if (userLevel != "admin") {
    Serial.println("Non-admin user attempted to add user");
    webServer.send(403, "text/plain", "Forbidden - Admin access required");
    return;
  }
  
  // CRITICAL: Check both "plain" and standard body content
  String requestBody = "";
  if (webServer.hasArg("plain")) {
    requestBody = webServer.arg("plain");
  } else {
    // Try to get body directly
    if (webServer.client().available()) {
      while (webServer.client().available()) {
        requestBody += (char)webServer.client().read();
      }
    }
  }

  // If we still don't have a request body, fail
  if (requestBody.isEmpty()) {
    Serial.println("No JSON data received in request body");
    webServer.send(400, "text/plain", "Missing JSON data in request body");
    return;
  }

  Serial.println("\nReceived request body: " + requestBody);
  
  // Parse JSON data
  StaticJsonDocument<256> reqDoc;
  DeserializationError error = deserializeJson(reqDoc, requestBody);
  if (error) {
    String errorMsg = "JSON parsing error: " + String(error.c_str());
    Serial.println(errorMsg);
    webServer.send(400, "text/plain", errorMsg);
    return;
  }

  // Process user data
  String newUsername = reqDoc["username"] | "";
  String newPassword = reqDoc["password"] | "";
  String newUserLevel = reqDoc["userLevel"] | "user";
  
  Serial.println("\nParsed user data:");
  Serial.print("Username: ");
  Serial.println(newUsername);
  Serial.print("Password: ");
  Serial.println(newPassword.isEmpty() ? "<not provided>" : "<provided>");
  Serial.print("User Level: ");
  Serial.println(newUserLevel);
  
  if (newUsername.isEmpty() || newPassword.isEmpty()) {
    webServer.send(400, "text/plain", "Username and password are required");
    return;
  }
  
  // Read existing users
  StaticJsonDocument<1024> doc;
  
  if (SD.exists(USERS_FILE)) {
    File file = SD.open(USERS_FILE);
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      
      if (error) {
        webServer.send(500, "text/plain", "Failed to read users file");
        return;
      }
    }
  }
  
  // Check if username already exists
  JsonArray users = doc["users"];
  for (JsonObject user : users) {
    if (user["username"] == newUsername) {
      webServer.send(400, "text/plain", "Username already exists");
      return;
    }
  }
  
  // Add new user
  JsonObject newUser = users.createNestedObject();
  newUser["username"] = newUsername;
  newUser["password"] = newPassword;
  newUser["userLevel"] = newUserLevel;
  
  // Write back to file
  File file = SD.open(USERS_FILE, FILE_WRITE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file for writing");
    return;
  }
  
  if (serializeJson(doc, file) == 0) {
    file.close();
    webServer.send(500, "text/plain", "Failed to write users file");
    return;
  }
  
  file.close();
  webServer.send(200, "text/plain", "User added successfully");
}

void handleUpdateUser() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  // Only admin users can update users
  if (userLevel != "admin") {
    webServer.send(403, "text/plain", "Forbidden");
    return;
  }
  
  // Get the username to update from URL parameter
  String targetUsername = "";
  if (webServer.uri().indexOf("/api/users/") == 0) {
    targetUsername = webServer.uri().substring(11);
    // URL decode
    targetUsername.replace("%20", " ");
  }
  
  if (targetUsername.isEmpty()) {
    webServer.send(400, "text/plain", "Missing username parameter");
    return;
  }
  
  // Parse the request
  StaticJsonDocument<256> reqDoc;
  if (webServer.hasArg("plain")) {
    DeserializationError error = deserializeJson(reqDoc, webServer.arg("plain"));
    if (error) {
      webServer.send(400, "text/plain", "Invalid JSON");
      return;
    }
  } else {
    webServer.send(400, "text/plain", "Missing JSON data");
    return;
  }
  
  // Username in the request body is only used if it matches the URL parameter
  String reqUsername = reqDoc["username"] | "";
  if (!reqUsername.isEmpty() && reqUsername != targetUsername) {
    webServer.send(400, "text/plain", "Username mismatch between URL and body");
    return;
  }
  
  String newPassword = reqDoc["password"] | "";
  String newUserLevel = reqDoc["userLevel"] | "";
  
  // Read existing users
  if (!SD.exists(USERS_FILE)) {
    webServer.send(404, "text/plain", "Users file not found");
    return;
  }
  
  File file = SD.open(USERS_FILE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file");
    return;
  }
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    webServer.send(500, "text/plain", "Failed to read users file");
    return;
  }
  
  // Find and update user
  bool userFound = false;
  JsonArray users = doc["users"];
  for (JsonObject user : users) {
    if (user["username"] == targetUsername) {
      if (!newPassword.isEmpty()) {
        user["password"] = newPassword;
      }
      if (!newUserLevel.isEmpty()) {
        user["userLevel"] = newUserLevel;
      }
      userFound = true;
      break;
    }
  }
  
  if (!userFound) {
    webServer.send(404, "text/plain", "User not found");
    return;
  }
  
  // Write back to file
  file = SD.open(USERS_FILE, FILE_WRITE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file for writing");
    return;
  }
  
  if (serializeJson(doc, file) == 0) {
    file.close();
    webServer.send(500, "text/plain", "Failed to write users file");
    return;
  }
  
  file.close();
  webServer.send(200, "text/plain", "User updated successfully");
}

void handleDeleteUser() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  // Only admin users can delete users
  if (userLevel != "admin") {
    webServer.send(403, "text/plain", "Forbidden");
    return;
  }
  
  // Get the username to delete from URL parameter
  String targetUsername = "";
  if (webServer.uri().indexOf("/api/users/") == 0) {
    targetUsername = webServer.uri().substring(11);
    // URL decode
    targetUsername.replace("%20", " ");
  }
  
  if (targetUsername.isEmpty()) {
    webServer.send(400, "text/plain", "Missing username parameter");
    return;
  }
  
  // Prevent deleting the last admin user
  if (targetUsername == username) {
    webServer.send(400, "text/plain", "Cannot delete your own account");
    return;
  }
  
  // Read existing users
  if (!SD.exists(USERS_FILE)) {
    webServer.send(404, "text/plain", "Users file not found");
    return;
  }
  
  File file = SD.open(USERS_FILE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file");
    return;
  }
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    webServer.send(500, "text/plain", "Failed to read users file");
    return;
  }
  
  // Find and remove user
  bool userFound = false;
  int adminCount = 0;
  JsonArray users = doc["users"];
  
  // First count admins
  for (JsonObject user : users) {
    if (user["userLevel"] == "admin") {
      adminCount++;
    }
  }
  
  // Check if we're trying to delete the last admin
  bool deletingLastAdmin = false;
  for (size_t i = 0; i < users.size(); i++) {
    if (users[i]["username"] == targetUsername) {
      if (users[i]["userLevel"] == "admin" && adminCount <= 1) {
        deletingLastAdmin = true;
        break;
      }
    }
  }
  
  if (deletingLastAdmin) {
    webServer.send(400, "text/plain", "Cannot delete the last admin user");
    return;
  }
  
  // Now actually remove the user
  for (size_t i = 0; i < users.size(); i++) {
    if (users[i]["username"] == targetUsername) {
      users.remove(i);
      userFound = true;
      break;
    }
  }
  
  if (!userFound) {
    webServer.send(404, "text/plain", "User not found");
    return;
  }
  
  // Write back to file
  file = SD.open(USERS_FILE, FILE_WRITE);
  if (!file) {
    webServer.send(500, "text/plain", "Failed to open users file for writing");
    return;
  }
  
  if (serializeJson(doc, file) == 0) {
    file.close();
    webServer.send(500, "text/plain", "Failed to write users file");
    return;
  }
  
  file.close();
  
  // Invalidate any sessions for this user
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].username == targetUsername) {
      sessions[i].isActive = false;
    }
  }
  
  webServer.send(200, "text/plain", "User deleted successfully");
}

void handleFileAccess() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.sendHeader("Location", "/login");
    webServer.send(302);
    return;
  }
  
  String path = webServer.uri();
  
  // Prevent directory traversal
  if (path.indexOf("..") != -1) {
    webServer.send(403, "text/plain", "Forbidden");
    return;
  }
  
  if (SD.exists(path)) {
    File file = SD.open(path);
    if (file.isDirectory()) {
      file.close();
      webServer.send(403, "text/plain", "Cannot access directory directly");
      return;
    }
    
    String contentType = "application/octet-stream";
    if (path.endsWith(".html") || path.endsWith(".htm")) contentType = "text/html";
    else if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".json")) contentType = "application/json";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) contentType = "image/jpeg";
    else if (path.endsWith(".gif")) contentType = "image/gif";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";
    else if (path.endsWith(".xml")) contentType = "text/xml";
    else if (path.endsWith(".pdf")) contentType = "application/pdf";
    else if (path.endsWith(".zip")) contentType = "application/zip";
    else if (path.endsWith(".txt")) contentType = "text/plain";
    
    webServer.streamFile(file, contentType);
    file.close();
    return;
  }
  
  webServer.send(404, "text/plain", "File not found");
}

// Setup function - called once at startup
void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  Serial.println("\nESP32 File Server starting...");
  
  // Initialize SD card
  if (!initSDCard()) {
    Serial.println("WARNING: SD card initialization failed!");
    // Continue anyway, as we might want to use the network features
  }
  
  // Setup authentication
  setupAuthentication();
  
  // Connect to WiFi or start Access Point
  if (!connectToWiFi()) {
    Serial.println("Failed to connect to WiFi, running in Access Point mode");
  }
  
  // Configure web server routes
  // Set CORS headers for all responses
  webServer.enableCORS(true);
  
  // Authentication routes
  webServer.on("/login", HTTP_GET, handleLogin);
  webServer.on("/login", HTTP_POST, handleLogin);
  webServer.on("/logout", HTTP_GET, handleLogout);
  
  // Main application routes
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/users", HTTP_GET, []() {
    String username, userLevel;
    if (!isAuthenticated(webServer, username, userLevel)) {
      // Get the current token if available
      String token = "";
      if (webServer.hasHeader("Authorization")) {
        token = webServer.header("Authorization").substring(7);
      } else if (webServer.hasArg("token")) {
        token = webServer.arg("token");
      }
      
      // If we have a token, include it in the redirect
      String redirectUrl = "/login";
      if (!token.isEmpty()) {
        redirectUrl += "?token=" + token;
      }
      redirectUrl += "&redirect=/users";
      
      webServer.sendHeader("Location", redirectUrl, true);
      webServer.send(302, "text/plain", "");
      return;
    }
    
    if (userLevel != "admin") {
      webServer.send(403, "text/plain", "Forbidden - Admin access required");
      return;
    }
    
    String html = String(user_management_html);
    html.replace("%USERNAME%", username);
    html.replace("%USERLEVEL%", userLevel);
    webServer.send(200, "text/html", html);
  });
  webServer.on("/server-info", HTTP_GET, handleServerInfo);
  
  // File management API
  webServer.on("/list", HTTP_GET, handleListFiles);
  webServer.on("/download", HTTP_GET, handleDownload);
  webServer.on("/delete", HTTP_DELETE, handleDelete);
  webServer.on("/create-dir", HTTP_POST, handleCreateDir);
  
  // User management API
  webServer.on("/api/users", HTTP_OPTIONS, []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    webServer.send(200);
  });
  
  webServer.on("/api/users", HTTP_GET, handleGetUsers);
  webServer.on("/api/users", HTTP_POST, handleAddUser);
  
  // Handle upload authentication and uploads
  webServer.on("/upload-auth", HTTP_GET, handleUploadAuth);
  
  webServer.on("/upload", HTTP_POST, 
    []() {
      webServer.send(200, "text/plain", "File uploaded successfully");
    },
    handleFileUpload
  );
  
  // For PUT and DELETE on dynamic paths, we'll use manual URL checking
  webServer.onNotFound([]() {
    String uri = webServer.uri();
    
    // Check for PUT and DELETE on /api/users/username paths
    if (uri.startsWith("/api/users/")) {
      if (webServer.method() == HTTP_PUT) {
        handleUpdateUser();
        return;
      } else if (webServer.method() == HTTP_DELETE) {
        handleDeleteUser();
        return;
      }
    }
    
    // If not a special API call, treat as a file access request
    handleFileAccess();
  });
  
  // Start web server
  webServer.begin(webServerPort);
  Serial.println("HTTP server started on port " + String(webServerPort));
  
  // Configure and start FTP server
  ftpSrv.begin(ftp_user, ftp_password);
  Serial.println("FTP server started");
  Serial.println("Username: " + String(ftp_user) + ", Password: " + String(ftp_password));
}

// Loop function - called repeatedly
void loop() {
  // Handle web server clients
  webServer.handleClient();
  
  // Handle FTP server clients
  ftpSrv.handleFTP();
  
  // Check if wifi connection is lost and try to reconnect
  if (!accessPointMode && WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    unsigned long currentMillis = millis();
    
    // Try to reconnect every 30 seconds
    if (currentMillis - lastReconnectAttempt > 30000) {
      lastReconnectAttempt = currentMillis;
      Serial.println("WiFi connection lost, trying to reconnect...");
      
      WiFi.disconnect();
      WiFi.reconnect();
      
      // Wait a bit to see if connection is established
      unsigned long startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nReconnected to WiFi!");
        Serial.print("New IP Address: ");
        Serial.println(WiFi.localIP());
        server_ip = WiFi.localIP().toString();
      } else {
        Serial.println("\nFailed to reconnect to WiFi.");
      }
    }
  }
}