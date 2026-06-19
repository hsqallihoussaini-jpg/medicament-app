const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080';

async function request(path, options = {}) {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    headers: {
      'Content-Type': 'application/json',
      ...(options.headers || {}),
    },
    ...options,
  });

  const rawBody = await response.text();
  let payload = null;

  if (rawBody) {
    try {
      payload = JSON.parse(rawBody);
    } catch {
      payload = { message: rawBody };
    }
  }

  if (!response.ok) {
    const message = payload?.error || payload?.message || 'Une erreur est survenue.';
    throw new Error(message);
  }

  return payload;
}

export function fetchMedicaments() {
  return request('/medicaments');
}

export function createMedicament(data) {
  return request('/medicaments', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function deleteMedicament(id) {
  return request(`/medicaments/${id}`, {
    method: 'DELETE',
  });
}

export function askQuestion(question) {
  return request('/chat', {
    method: 'POST',
    body: JSON.stringify({ question }),
  });
}

export function fetchPrises(medicamentId) {
  const suffix = medicamentId ? `?medicament_id=${encodeURIComponent(medicamentId)}` : '';
  return request(`/prises${suffix}`);
}

export function createPrise(data) {
  return request('/prises', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function fetchStats() {
  return request('/stats');
}

export function updateMedicamentStock(id, quantite_restante) {
  return request(`/medicaments/${id}/stock`, {
    method: 'PATCH',
    body: JSON.stringify({ quantite_restante }),
  });
}

export function shareRappel(data) {
  return request('/partage-rappel', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function fetchRendezvous() {
  return request('/rendezvous');
}

export function createRendezvous(data) {
  return request('/rendezvous', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function deleteRendezvous(id) {
  return request(`/rendezvous/${id}`, {
    method: 'DELETE',
  });
}
