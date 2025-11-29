# plot_perf.py
import pandas as pd
import matplotlib.pyplot as plt
df = pd.read_csv('timings_mpi.csv')
# Strong
strong = df[df['mode']=='strong'].copy()
strong['P']=strong['P'].astype(int)
strong = strong.sort_values('P')
t1 = strong[strong['P']==1]['time_ms'].values[0]
strong['speedup'] = t1 / strong['time_ms']
strong['eff'] = strong['speedup'] / strong['P'] * 100
plt.figure()
plt.plot(strong['P'], strong['time_ms'], marker='o'); plt.xlabel('P'); plt.ylabel('time ms'); plt.title('Strong scaling time')
plt.savefig('strong_time.png', dpi=150)
plt.figure()
plt.plot(strong['P'], strong['speedup'], marker='o'); plt.plot(strong['P'], strong['P'],'--'); plt.xlabel('P'); plt.ylabel('speedup'); plt.title('Strong scaling speedup')
plt.savefig('strong_speedup.png', dpi=150)
plt.figure()
plt.plot(strong['P'], strong['eff'], marker='o'); plt.xlabel('P'); plt.ylabel('efficiency %'); plt.title('Strong scaling efficiency')
plt.savefig('strong_eff.png', dpi=150)

# Weak
weak = df[df['mode']=='weak'].copy()
weak['P']=weak['P'].astype(int)
weak = weak.sort_values('P')
plt.figure(); plt.plot(weak['P'], weak['time_ms'], marker='o'); plt.xlabel('P'); plt.ylabel('time ms'); plt.title('Weak scaling time'); plt.savefig('weak_time.png', dpi=150)
