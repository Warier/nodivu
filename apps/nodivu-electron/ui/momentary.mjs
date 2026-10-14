// A renewable host lease closes even if this renderer crashes. No document edits.
export function createHold(send,{interval=100,schedule=setInterval,cancel=clearInterval,onState=()=>{}}={}){
 let timer=null,pressed=false,inFlight=false;
 async function pulse(value){if(value&&inFlight)return;inFlight=true;try{await send(value);}catch{release();}finally{inFlight=false;}}
 function release(){if(!pressed)return;pressed=false;cancel(timer);timer=null;onState(false);void pulse(false);}
 return {press(){if(pressed)return;pressed=true;onState(true);void pulse(true);timer=schedule(()=>void pulse(true),interval);},release,get pressed(){return pressed;}};
}
export function mountMomentary(button,{label,send}){
 button.type='button';button.textContent=label;button.setAttribute('aria-pressed','false');
 const controller=createHold(send,{onState:value=>{button.setAttribute('aria-pressed',String(value));button.classList.toggle('held',value);button.textContent=value?'Falando…':label;}});
 const release=()=>controller.release();
 button.onpointerdown=e=>{if(e.button!==0)return;e.preventDefault();button.focus();button.setPointerCapture(e.pointerId);controller.press();};
 button.onpointerup=button.onpointercancel=button.onlostpointercapture=button.onblur=release;
 button.onkeydown=e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();if(!e.repeat)controller.press();}};
 button.onkeyup=e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();release();}};
 // One global lifecycle listener per mounted control, removed once its surface disappears.
 const abort=new AbortController();window.addEventListener('blur',release,{signal:abort.signal});
 document.addEventListener('visibilitychange',()=>{if(document.hidden)release();},{signal:abort.signal});
 const observer=new MutationObserver(()=>{if(!button.isConnected){release();abort.abort();observer.disconnect();}});
 queueMicrotask(()=>{if(button.isConnected)observer.observe(document.body,{childList:true,subtree:true});});
}
