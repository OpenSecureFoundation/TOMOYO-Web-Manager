# TOMOYO-Web-Manager
Conception, développement et déploiement d’une Plateforme web de gestion et visualisation des politiques TOMOYO



# Fonctionnalités
1. Visualisation des policies
 
2. Mode apprentissage → enforcement
bouton :
“Learning mode” 
“Enforcement mode” 

3. Génération automatique de règles
lancer une app 
capturer accès 
générer policy 

4. Édition web des règles (avec validation obligatoire)



# Partie ATTACK 

Scénario :

app web vulnérable (lecture fichiers) 

L’équipe tente :

lire /etc/shadow 

exécuter /bin/bash 



# Partie DEFENSE
Avec TOMOYO :

autoriser uniquement :
/var/www/app 

bloquer :
/etc 
/root
