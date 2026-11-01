document.getElementById('run').addEventListener('click', async () => {
  const output = document.getElementById('results');
  output.textContent = 'Checking...';
  try { output.textContent = await Probe.inspect(chrome); }
  catch { output.textContent = 'The capability check could not complete.'; }
});
