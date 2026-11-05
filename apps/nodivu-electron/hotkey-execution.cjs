// Check at dispatch, not just when a keystroke arrives. An expired request may
// already have completed part of a compound action; never claim it was undone.
function assertExecution(execution, pending, now=Date.now()) {
  if (execution === undefined) return;
  if (!execution || !pending || execution.token !== pending.token ||
      execution.deadline !== pending.deadline || !Number.isSafeInteger(execution.deadline) || now >= execution.deadline) {
    throw new Error('Atalho expirou; nenhuma nova etapa foi enviada. Etapas já iniciadas podem ter sido aplicadas.');
  }
}
module.exports={assertExecution};
