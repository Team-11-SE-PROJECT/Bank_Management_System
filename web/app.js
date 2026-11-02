const app = document.querySelector("#app");
const money = (paise) => {
  const value = BigInt(paise);
  const negative = value < 0n;
  const magnitude = negative ? -value : value;
  const rupees = magnitude / 100n;
  const fraction = String(magnitude % 100n).padStart(2, "0");
  return `₹${negative ? "-" : ""}${new Intl.NumberFormat("en-IN").format(rupees)}.${fraction}`;
};
const escapeHtml = (value) => String(value ?? "").replace(/[&<>"']/g, (char) => ({
  "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
})[char]);

async function api(path, values, method = values ? "POST" : "GET") {
  const options = { method, credentials: "same-origin", headers: {} };
  if (values) {
    options.headers["Content-Type"] = "application/x-www-form-urlencoded";
    options.body = new URLSearchParams(values).toString();
  }
  const response = await fetch(path, options);
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error("The server returned an unreadable response.");
  }
  if (!response.ok || result.ok === false) {
    const error = new Error(result.message || "The request could not be completed.");
    error.status = response.status;
    throw error;
  }
  return result;
}

function showMessage(container, text, success = false) {
  const message = container.querySelector("[data-message]");
  if (!message) return;
  message.textContent = text;
  message.className = success ? "form-success" : "form-error";
  message.classList.remove("hidden");
}

function clearMessage(container) {
  const message = container.querySelector("[data-message]");
  if (message) {
    message.textContent = "";
    message.className = "hidden";
  }
}

function bindForm(form, action, onSuccess, beforeSubmit) {
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    clearMessage(form);
    const submit = form.querySelector('[type="submit"]');
    submit.disabled = true;
    try {
      const values = Object.fromEntries(new FormData(form).entries());
      if (beforeSubmit && !beforeSubmit(values)) return;
      const result = await api(action, values);
      form.reset();
      if (onSuccess) await onSuccess(result);
      else showMessage(form, "Done. Your changes have been saved.", true);
    } catch (error) {
      showMessage(form, error.message);
    } finally {
      submit.disabled = false;
    }
  });
}

function loginView() {
  app.innerHTML = `
    <div class="login-layout">
      <section class="intro">
        <div class="eyebrow">A calmer way to bank</div>
        <h1>Your money.<br>In good hands.</h1>
        <p class="intro-copy">A clear, secure place to manage your everyday banking. Sign in to see your account, move money, and keep track of what matters.</p>
        <ul class="feature-list">
          <li><span class="feature-check">✓</span> Your balance, at a glance</li>
          <li><span class="feature-check">✓</span> Simple, recorded transactions</li>
          <li><span class="feature-check">✓</span> Protected customer and admin access</li>
          <li><span class="feature-check">✓</span> Your data stays on this device</li>
        </ul>
      </section>
      <section class="card login-card">
        <div class="card-heading">
          <h2>Welcome back</h2>
          <p>Sign in to your Northstar account.</p>
        </div>
        <form id="login-form">
          <div class="hidden" data-message></div>
          <div class="field">
            <label for="role">I am signing in as</label>
            <select id="role" name="role" required>
              <option value="CUSTOMER">Customer</option>
              <option value="ADMIN">Administrator</option>
            </select>
          </div>
          <div class="field">
            <label for="userId">Customer ID / Admin ID</label>
            <input id="userId" name="userId" autocomplete="username" placeholder="Enter your login ID" required>
          </div>
          <div class="field">
            <label for="password">PIN or password</label>
            <input id="password" name="password" type="password" autocomplete="current-password" placeholder="Enter your PIN or password" required>
          </div>
          <button class="button button-block" type="submit">Sign in <span aria-hidden="true">→</span></button>
          <p class="login-note">For your safety, sign out when you’re finished. This demonstration is intended for local use only.</p>
        </form>
      </section>
    </div>`;

  bindForm(document.querySelector("#login-form"), "/api/login", async () => {
    await renderDashboard();
  });
}

function transactionRows(items) {
  if (!items.length) return '<div class="empty-state">No transactions match your search.</div>';
  return items.map((item) => {
    const withdrawal = item.type === "WITHDRAWAL";
    const transfer = item.counterparty !== "-" &&
      item.counterparty !== "CASH" &&
      item.counterparty !== "CHEQUE" &&
      item.counterparty !== "TRANSFER_REVERSAL";
    const label = transfer
      ? `${withdrawal ? "Transfer to" : "Transfer from"} ${escapeHtml(item.counterparty)}`
      : withdrawal ? "Withdrawal" : "Deposit";
    const sign = withdrawal ? "−" : "+";
    return `<article class="transaction-row">
      <span class="transaction-icon ${withdrawal ? "withdrawal" : "deposit"}" aria-hidden="true">${withdrawal ? "↗" : "↙"}</span>
      <div>
        <div class="transaction-name">${label} <span aria-label="transaction ID">${escapeHtml(item.txnId)}</span></div>
        <div class="transaction-meta">${escapeHtml(item.timestamp)} · ${item.counterparty === "CASH" || item.counterparty === "CHEQUE" ? `Via ${escapeHtml(item.counterparty.toLowerCase())}` : transfer ? "Account transfer" : "Bank transaction"}</div>
      </div>
      <div class="transaction-values">
        <div class="transaction-amount ${withdrawal ? "negative" : ""}">${sign}${money(item.amountPaise)}</div>
        <div class="transaction-meta">Balance ${money(item.balanceAfterPaise)}</div>
      </div>
    </article>`;
  }).join("");
}

function bindTransactionHistory(initialItems) {
  const list = document.querySelector("#transaction-list");
  const search = document.querySelector("#history-search");
  const filterForm = document.querySelector("#history-filter");
  const pageLabel = document.querySelector("#history-page");
  const previous = document.querySelector("#history-previous");
  const next = document.querySelector("#history-next");
  const message = document.querySelector("#history-message");
  let items = initialItems;
  let page = 0;
  const pageSize = 10;

  const render = () => {
    const query = search.value.trim().toLowerCase();
    const filtered = items.filter((item) =>
      [item.txnId, item.timestamp, item.counterparty, item.type]
        .some((value) => String(value).toLowerCase().includes(query))
    );
    const pageCount = Math.max(1, Math.ceil(filtered.length / pageSize));
    page = Math.min(page, pageCount - 1);
    list.innerHTML = transactionRows(filtered.slice(page * pageSize, (page + 1) * pageSize));
    pageLabel.textContent = filtered.length
      ? `${page * pageSize + 1}–${Math.min((page + 1) * pageSize, filtered.length)} of ${filtered.length}`
      : "0 transactions";
    previous.disabled = page === 0;
    next.disabled = page >= pageCount - 1;
  };

  filterForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    message.textContent = "";
    message.className = "hidden";
    const values = Object.fromEntries(new FormData(filterForm).entries());
    try {
      const result = await api("/api/history", values);
      items = result.transactions;
      page = 0;
      render();
      document.querySelector("#history-total").textContent =
        `${items.length} matching`;
    } catch (error) {
      message.textContent = error.message;
      message.className = "form-error";
    }
  });
  search.addEventListener("input", () => { page = 0; render(); });
  previous.addEventListener("click", () => { page -= 1; render(); });
  next.addEventListener("click", () => { page += 1; render(); });
  render();
}

function customerDashboard(data) {
  const account = data.account;
  const transactions = data.transactions || [];
  const deposits = transactions
    .filter((item) => item.type === "DEPOSIT")
    .reduce((total, item) => total + BigInt(item.amountPaise), 0n);
  const withdrawals = transactions
    .filter((item) => item.type === "WITHDRAWAL")
    .reduce((total, item) => total + BigInt(item.amountPaise), 0n);
  const accountSection = account ? `
    <section class="card balance-card">
      <div class="balance-topline"><div class="balance-label">Your available balance</div><span class="status-pill">${escapeHtml(account.status)}</span></div>
      <div class="balance-value">${money(account.balancePaise)}</div>
      <div class="balance-account">
        <span>ACCOUNT NUMBER <strong>${escapeHtml(account.accountNo)}</strong></span>
        <span>PERSONAL ACCOUNT</span>
      </div>
    </section>
    <div class="activity-summary">
      <article class="card activity-stat"><span class="activity-stat-icon">↓</span><div><span>Total deposited</span><strong>${money(deposits.toString())}</strong></div></article>
      <article class="card activity-stat"><span class="activity-stat-icon outgoing">↑</span><div><span>Total withdrawn</span><strong>${money(withdrawals.toString())}</strong></div></article>
    </div>` : `
    <section class="card section-card">
      <div class="section-title"><h2>No linked bank account</h2></div>
      <p class="intro-copy">Your login is valid, but no account is linked to it yet. Please contact an administrator.</p>
    </section>`;

  app.innerHTML = `
    <div class="dashboard-header">
      <div><div class="eyebrow">YOUR PERSONAL BANKING</div><h1>Good ${new Date().getHours() < 12 ? "morning" : new Date().getHours() < 18 ? "afternoon" : "evening"}, ${escapeHtml(data.userId)}</h1><p>Your money, thoughtfully managed.</p></div>
      <div class="header-actions"><button class="button button-secondary" id="refresh-button" type="button"><span aria-hidden="true">↻</span> Refresh</button><button class="button button-secondary" id="logout-button" type="button">Sign out <span aria-hidden="true">→</span></button></div>
    </div>
    <div class="dashboard-grid">
      <div class="stack">
        ${accountSection}
        ${account ? `
        <section class="card section-card history-card">
          <div class="section-title"><div><div class="eyebrow">YOUR MONEY, IN MOTION</div><h2>Transaction history</h2></div><span id="history-total" class="history-count">${transactions.length} total</span></div>
          <form id="history-filter" class="history-filter">
            <div class="field"><label for="history-from">From</label><input id="history-from" name="fromDate" type="date"></div>
            <div class="field"><label for="history-to">To</label><input id="history-to" name="toDate" type="date"></div>
            <div class="field"><label for="history-type">Type</label><select id="history-type" name="type"><option value="ALL">All activity</option><option value="DEPOSIT">Deposits</option><option value="WITHDRAWAL">Withdrawals</option></select></div>
            <button class="button button-secondary" type="submit">Apply filters</button>
          </form>
          <div class="history-tools">
            <label class="history-search-wrap"><span aria-hidden="true">⌕</span><input id="history-search" type="search" placeholder="Search ID, date or method" aria-label="Search transaction history"></label>
          </div>
          <div id="history-message" class="hidden"></div>
          <div id="transaction-list" class="transaction-list" aria-live="polite"></div>
          <div class="history-pagination"><span id="history-page"></span><div><button class="page-button" id="history-previous" type="button" aria-label="Previous page">←</button><button class="page-button" id="history-next" type="button" aria-label="Next page">→</button></div></div>
        </section>` : ""}
        ${account ? `
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">YOUR PROFILE</div><h2>Account details</h2></div><span>${escapeHtml(account.customerId)}</span></div>
          <form id="profile-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="profile-name">Full name</label><input id="profile-name" name="name" autocomplete="name" value="${escapeHtml(account.name)}" required></div>
            <div class="field"><label for="profile-phone">Phone number</label><input id="profile-phone" name="phone" inputmode="numeric" maxlength="10" value="${escapeHtml(account.phone)}" required></div>
            <div class="field"><label for="profile-address">Address</label><input id="profile-address" name="address" autocomplete="street-address" value="${escapeHtml(account.address)}" required></div>
            <button class="button button-secondary button-block" type="submit">Save profile</button>
          </form>
        </section>` : ""}
        ${account && account.status !== "ACTIVE" ? `
        <section class="card section-card">
          <div class="section-title"><h2>Account is closed</h2><span class="status-pill">${escapeHtml(account.status)}</span></div>
          <p class="helper-copy">Transactions and transfers are unavailable for this account. Contact an administrator if you need help.</p>
        </section>` : ""}
      </div>
      <div class="stack">
        ${account && account.status === "ACTIVE" ? `
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">QUICK ACTIONS</div><h2>Move money</h2></div></div>
          <div class="tab-row" role="tablist" aria-label="Transaction type">
            <button type="button" class="tab active" data-type="DEPOSIT">Deposit</button>
            <button type="button" class="tab" data-type="WITHDRAWAL">Withdraw</button>
          </div>
          <form id="transaction-form">
            <div class="hidden" data-message></div>
            <input type="hidden" name="type" value="DEPOSIT">
            <div class="field"><label for="amount">Amount (INR)</label><input id="amount" name="amount" inputmode="decimal" placeholder="e.g. 250.00" required></div>
            <div class="field" id="mode-field"><label for="mode">Deposit method</label><select id="mode" name="mode"><option value="CASH">Cash</option><option value="CHEQUE">Cheque</option></select></div>
            <button class="button button-block" type="submit">Deposit funds</button>
            <p class="helper-copy">Amounts are validated and saved to your transaction history.</p>
          </form>
        </section>` : ""}
        ${account && account.status === "ACTIVE" ? `
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">BETWEEN NORTHSTAR ACCOUNTS</div><h2>Transfer money</h2></div></div>
          <form id="transfer-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="destination-account">Recipient account number</label><input id="destination-account" name="destinationAccountNo" autocomplete="off" placeholder="Enter destination account" required></div>
            <div class="field"><label for="transfer-amount">Amount (INR)</label><input id="transfer-amount" name="amount" inputmode="decimal" placeholder="e.g. 500.00" required></div>
            <button class="button button-block" type="submit">Review &amp; transfer</button>
            <p class="helper-copy">Transfers are recorded in the history of both accounts. Check the recipient account before continuing.</p>
          </form>
        </section>` : ""}
        <section class="card section-card">
          <div class="section-title"><h2>Update your PIN</h2></div>
          <form id="pin-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="currentPin">Current PIN</label><input id="currentPin" name="currentPin" type="password" autocomplete="current-password" required></div>
            <div class="field"><label for="newPin">New PIN</label><input id="newPin" name="newPin" type="password" inputmode="numeric" autocomplete="new-password" minlength="4" maxlength="6" required></div>
            <button class="button button-secondary button-block" type="submit">Change PIN</button>
          </form>
        </section>
      </div>
    </div>`;

  document.querySelector("#refresh-button").addEventListener("click", renderDashboard);
  document.querySelector("#logout-button").addEventListener("click", logout);
  if (account) bindTransactionHistory(transactions);
  const profileForm = document.querySelector("#profile-form");
  if (profileForm) {
    bindForm(profileForm, "/api/customer/update", async () => {
      await renderDashboard();
    });
  }
  const transactionForm = document.querySelector("#transaction-form");
  if (transactionForm) {
    document.querySelectorAll("[data-type]").forEach((tab) => tab.addEventListener("click", () => {
      document.querySelectorAll("[data-type]").forEach((item) => item.classList.remove("active"));
      tab.classList.add("active");
      const type = tab.dataset.type;
      transactionForm.elements.type.value = type;
      document.querySelector("#mode-field").classList.toggle("hidden", type !== "DEPOSIT");
      transactionForm.querySelector('[type="submit"]').textContent =
        type === "DEPOSIT" ? "Deposit funds" : "Withdraw funds";
      clearMessage(transactionForm);
    }));
    bindForm(transactionForm, "/api/transaction", async (result) => {
      await renderDashboard();
      const notice = document.createElement("div");
      notice.className = "form-success";
      notice.textContent = `Transaction ${result.txnId} completed. Updated balance: ${money(result.balancePaise)}.`;
      app.querySelector(".dashboard-header").after(notice);
    });
  }
  bindForm(document.querySelector("#pin-form"), "/api/change-pin");
  const transferForm = document.querySelector("#transfer-form");
  if (transferForm) {
    bindForm(
      transferForm,
      "/api/transfer",
      async (result) => {
        await renderDashboard();
        const notice = document.createElement("div");
        notice.className = "form-success";
        notice.textContent =
          `Transfer ${result.txnId} completed. Updated balance: ${money(result.balancePaise)}.`;
        app.querySelector(".dashboard-header").after(notice);
      },
      (values) => window.confirm(
        `Transfer ₹${values.amount} to account ${values.destinationAccountNo}?`
      )
    );
  }
}

async function adminDashboard(data) {
  app.innerHTML = `
    <div class="dashboard-header">
      <div><div class="eyebrow">NORTHSTAR · ADMINISTRATION</div><h1>Welcome, ${escapeHtml(data.userId)}</h1><p>Manage customer accounts and keep an eye on system health.</p></div>
      <div class="header-actions"><button class="button button-secondary" id="refresh-button" type="button"><span aria-hidden="true">↻</span> Refresh</button><button class="button button-secondary" id="logout-button" type="button">Sign out <span aria-hidden="true">→</span></button></div>
    </div>
    <div class="admin-grid">
      <div class="stack">
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">NEW RELATIONSHIP</div><h2>Create a customer account</h2></div><span>New customer setup</span></div>
          <form id="create-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="name">Customer full name</label><input id="name" name="name" autocomplete="name" required></div>
            <div class="field"><label for="phone">Phone number</label><input id="phone" name="phone" inputmode="numeric" maxlength="10" placeholder="10 digits" required></div>
            <div class="field"><label for="address">Address</label><input id="address" name="address" autocomplete="street-address" required></div>
            <div class="field"><label for="initialPin">Initial customer PIN</label><input id="initialPin" name="initialPin" type="password" inputmode="numeric" minlength="4" maxlength="6" required></div>
            <button class="button button-block" type="submit">Create account</button>
          </form>
        </section>
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">ACCESS SUPPORT</div><h2>Unlock customer login</h2></div><span>Failed sign-in recovery</span></div>
          <form id="unlock-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="unlockUserId">Customer login ID</label><input id="unlockUserId" name="userId" placeholder="e.g. CUST0001" required></div>
            <button class="button button-secondary button-block" type="submit">Unlock account</button>
          </form>
        </section>
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">CUSTOMER SERVICES</div><h2>Find and manage customers</h2></div></div>
          <form id="customer-search-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="customer-search-field">Search by</label><select id="customer-search-field" name="field"><option value="CUSTOMER_ID">Customer ID</option><option value="CUSTOMER_NAME">Name contains</option><option value="PHONE">Phone number</option><option value="ACCOUNT_NUMBER">Account number</option></select></div>
            <div class="field"><label for="customer-search-query">Search value</label><input id="customer-search-query" name="query" placeholder="Enter customer details" required></div>
            <button class="button button-secondary button-block" type="submit">Search customers</button>
          </form>
          <div id="customer-search-results" class="search-results" aria-live="polite"></div>
        </section>
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">COMPLIANCE</div><h2>Audit log review</h2></div></div>
          <form id="audit-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="audit-date">Date</label><input id="audit-date" name="date" type="date"></div>
            <div class="field"><label for="audit-event">Event contains</label><input id="audit-event" name="event" placeholder="e.g. TRANSFER"></div>
            <div class="field"><label for="audit-account">Account / user</label><input id="audit-account" name="account" placeholder="Optional"></div>
            <div class="field"><label for="audit-result">Result contains</label><input id="audit-result" name="result" placeholder="e.g. SUCCESS"></div>
            <button class="button button-secondary button-block" type="submit">Review audit records</button>
          </form>
          <div id="audit-results" class="audit-results" aria-live="polite"></div>
        </section>
      </div>
      <div class="stack">
        <section class="card section-card">
          <div class="section-title"><div><div class="eyebrow">OPERATIONS</div><h2>System status</h2></div><span>Live local data</span></div>
          <div id="status-content"><div class="loading-card"><span class="spinner"></span><p>Checking system status…</p></div></div>
        </section>
        <section class="card section-card">
          <div class="section-title"><h2>Change administrator PIN</h2></div>
          <form id="pin-form">
            <div class="hidden" data-message></div>
            <div class="field"><label for="currentPin">Current PIN</label><input id="currentPin" name="currentPin" type="password" autocomplete="current-password" required></div>
            <div class="field"><label for="newPin">New PIN</label><input id="newPin" name="newPin" type="password" inputmode="numeric" autocomplete="new-password" minlength="4" maxlength="6" required></div>
            <button class="button button-secondary button-block" type="submit">Change PIN</button>
          </form>
        </section>
      </div>
    </div>`;

  document.querySelector("#refresh-button").addEventListener("click", renderDashboard);
  document.querySelector("#logout-button").addEventListener("click", logout);
  bindForm(document.querySelector("#create-form"), "/api/admin/create-account", async (result) => {
    showMessage(
      document.querySelector("#create-form"),
      `Account created — customer ID: ${result.customerId}, account number: ${result.accountNo}.`,
      true
    );
    await loadAdminStatus();
  });
  bindForm(document.querySelector("#unlock-form"), "/api/admin/unlock");
  bindForm(document.querySelector("#pin-form"), "/api/change-pin");
  bindCustomerSearch();
  bindAuditReview();
  await loadAdminStatus();
}

function customerSearchCards(records) {
  return records.map(({ customer, accounts }) => `
    <article class="customer-result">
      <div class="customer-result-heading">
        <div><h3>${escapeHtml(customer.name)}</h3><span>${escapeHtml(customer.customerId)}</span></div>
        <span>${accounts.length} account${accounts.length === 1 ? "" : "s"}</span>
      </div>
      <form class="customer-edit-form" data-customer-id="${escapeHtml(customer.customerId)}">
        <div class="hidden" data-message></div>
        <input type="hidden" name="customerId" value="${escapeHtml(customer.customerId)}">
        <div class="field"><label>Name</label><input name="name" value="${escapeHtml(customer.name)}" required></div>
        <div class="field"><label>Phone</label><input name="phone" inputmode="numeric" maxlength="10" value="${escapeHtml(customer.phone)}" required></div>
        <div class="field"><label>Address</label><input name="address" value="${escapeHtml(customer.address)}" required></div>
        <button class="button button-secondary button-block" type="submit">Save customer details</button>
      </form>
      <div class="customer-accounts">
        ${accounts.length ? accounts.map((account) => {
          const canClose = account.status === "ACTIVE" && BigInt(account.balancePaise) === 0n;
          return `<div class="customer-account-row">
            <div><strong>${escapeHtml(account.accountNo)}</strong><span>${escapeHtml(account.status)} · ${money(account.balancePaise)}</span></div>
            <button class="button button-danger close-account-button" type="button" data-account-no="${escapeHtml(account.accountNo)}" ${canClose ? "" : "disabled"}>${account.status === "CLOSED" ? "Closed" : "Close account"}</button>
          </div>`;
        }).join("") : '<p class="helper-copy">No linked accounts.</p>'}
      </div>
    </article>`).join("");
}

function bindCustomerSearch() {
  const form = document.querySelector("#customer-search-form");
  const results = document.querySelector("#customer-search-results");
  let lastValues = null;
  const runSearch = async (values) => {
    try {
      const result = await api("/api/admin/search-customers", values);
      results.innerHTML = customerSearchCards(result.results);
      results.querySelectorAll(".customer-edit-form").forEach((editForm) => {
        bindForm(editForm, "/api/customer/update", async () => {
          await runSearch(lastValues);
        });
      });
      results.querySelectorAll(".close-account-button").forEach((button) => {
        button.addEventListener("click", async () => {
          if (!window.confirm(`Close account ${button.dataset.accountNo}? Only an account with zero balance can be closed.`)) return;
          button.disabled = true;
          try {
            await api("/api/admin/close-account", {
              accountNo: button.dataset.accountNo,
              confirmed: "yes"
            });
            await runSearch(lastValues);
            await loadAdminStatus();
          } catch (error) {
            const message = document.createElement("div");
            message.className = "form-error";
            message.textContent = error.message;
            button.closest(".customer-result").prepend(message);
            button.disabled = false;
          }
        });
      });
    } catch (error) {
      results.innerHTML = `<div class="form-error">${escapeHtml(error.message)}</div>`;
    }
  };
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    clearMessage(form);
    lastValues = Object.fromEntries(new FormData(form).entries());
    await runSearch(lastValues);
  });
}

function bindAuditReview() {
  const form = document.querySelector("#audit-form");
  const results = document.querySelector("#audit-results");
  let records = [];
  let page = 0;
  const pageSize = 10;

  const render = () => {
    const pageRecords = records.slice(page * pageSize, (page + 1) * pageSize);
    const rows = pageRecords.map((record) => `
      <tr>
        <td>${escapeHtml(record.timestamp)}</td>
        <td>${escapeHtml(record.event)}</td>
        <td>${escapeHtml(record.account)}</td>
        <td>${escapeHtml(record.result)}</td>
        <td>${escapeHtml(record.details)}</td>
      </tr>`).join("");
    results.innerHTML = records.length ? `
      <p class="helper-copy">${records.length} matching audit record${records.length === 1 ? "" : "s"}</p>
      <div class="table-scroll"><table class="audit-table"><thead><tr><th>Time</th><th>Event</th><th>Account / user</th><th>Result</th><th>Details</th></tr></thead><tbody>${rows}</tbody></table></div>
      <div class="history-pagination"><span>${page * pageSize + 1}–${Math.min((page + 1) * pageSize, records.length)} of ${records.length}</span><div><button class="page-button" data-audit-page="-1" type="button" aria-label="Previous audit page" ${page === 0 ? "disabled" : ""}>←</button><button class="page-button" data-audit-page="1" type="button" aria-label="Next audit page" ${page >= Math.ceil(records.length / pageSize) - 1 ? "disabled" : ""}>→</button></div></div>` :
      '<div class="empty-state">No matching audit records.</div>';
    results.querySelectorAll("[data-audit-page]").forEach((button) => {
      button.addEventListener("click", () => {
        page += Number(button.dataset.auditPage);
        render();
      });
    });
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    clearMessage(form);
    const submit = form.querySelector('[type="submit"]');
    submit.disabled = true;
    try {
      const result = await api(
        "/api/admin/audit",
        Object.fromEntries(new FormData(form).entries())
      );
      records = result.records;
      page = 0;
      render();
    } catch (error) {
      showMessage(form, error.message);
    } finally {
      submit.disabled = false;
    }
  });
}

async function loadAdminStatus() {
  const target = document.querySelector("#status-content");
  if (!target) return;
  try {
    const status = await api("/api/admin/status");
    const fileRow = (label, available) => `<div class="file-status-row"><span>${label}</span><strong class="${available ? "file-ok" : "file-missing"}">${available ? "Available" : "Unavailable"}</strong></div>`;
    target.innerHTML = `
      <div class="admin-stats">
        <div class="stat-card"><div class="stat-label">Customers</div><div class="stat-number">${status.customerCount}</div></div>
        <div class="stat-card"><div class="stat-label">Accounts</div><div class="stat-number">${status.accountCount}</div></div>
        <div class="stat-card"><div class="stat-label">Logins</div><div class="stat-number">${status.credentialCount}</div></div>
      </div>
      <div class="file-status">
        ${fileRow("Customer data", status.customersFileAccessible)}
        ${fileRow("Account data", status.accountsFileAccessible)}
        ${fileRow("Credential data", status.credentialsFileAccessible)}
      </div>`;
  } catch (error) {
    target.innerHTML = `<div class="form-error">${escapeHtml(error.message)}</div>`;
  }
}

async function renderDashboard() {
  try {
    const data = await api("/api/me");
    if (data.role === "ADMIN") await adminDashboard(data);
    else customerDashboard(data);
  } catch (error) {
    if (error.status === 401) {
      loginView();
      return;
    }
    app.innerHTML = `<section class="card section-card"><div class="form-error">${escapeHtml(error.message)}</div><button class="button button-secondary" id="retry-button" type="button">Try again</button></section>`;
    document.querySelector("#retry-button").addEventListener("click", renderDashboard);
  }
}

async function logout() {
  try {
    await api("/api/logout", {});
  } catch {
    // The local page returns to sign-in even if the session already expired.
  }
  loginView();
}

renderDashboard();
