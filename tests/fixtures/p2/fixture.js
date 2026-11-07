// Hand-authored, license-safe corpus resource. The mutation is an execution smoke, not DOM conformance coverage.
const marker = document.createElement('p');
marker.id = 'mutated';
marker.textContent = 'External script mutation marker';
document.body.appendChild(marker);
